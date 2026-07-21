#include "zygisk.hpp"
#include "bind_hook.h"
#include "config_store.h"
#include "denylist_watcher.h"
#include "log.h"

#include <sys/inotify.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include <string>

using namespace isolpolicy;

// ---------------------------------------------------------------------------
// Protokol IPC ke companion root, dua mode dibedakan lewat 1 byte trigger:
//
// Mode SNAPSHOT (kTriggerSnapshot) — dipakai preServerSpecialize untuk
// ambil denylist awal sekali saat system_server baru mulai fork:
//   client -> companion : 1 byte kTriggerSnapshot
//   companion -> client : uint32_t count, lalu per entri [uint32_t len][bytes]
//   (socket ditutup segera setelah itu oleh kedua sisi)
//
// Mode WATCH (kTriggerWatch) — dipakai untuk reload live tanpa reboot.
// Socket ini dibuka SEKALI di preServerSpecialize (karena connectCompanion()
// cuma valid di situ) lalu dipertahankan hidup selama system_server hidup:
//   client -> companion : 1 byte kTriggerWatch
//   companion -> client : SEGERA kirim snapshot saat ini (sama seperti
//                          mode SNAPSHOT), lalu companion masuk ke inotify
//                          loop dan mengirim snapshot baru setiap kali
//                          kDenylistFile berubah — berulang kali, selama
//                          socket masih terbuka. Tidak pernah selesai
//                          sendiri; hanya berhenti kalau system_server mati
//                          atau companion di-restart (mis. modul di-update).
// ---------------------------------------------------------------------------

static constexpr uint8_t kTriggerSnapshot = 1;
static constexpr uint8_t kTriggerWatch = 2;

static bool WriteAll(int fd, const void *buf, size_t len) {
    auto p = static_cast<const char *>(buf);
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return false;
        p += n;
        len -= n;
    }
    return true;
}

static bool ReadAll(int fd, void *buf, size_t len) {
    auto p = static_cast<char *>(buf);
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) return false;
        p += n;
        len -= n;
    }
    return true;
}

// Kirim satu snapshot denylist saat ini lewat fd. Dipakai oleh kedua mode.
static bool SendSnapshot(int fd) {
    std::unordered_set<std::string> denied = LoadDenylistFromDisk();
    uint32_t count = static_cast<uint32_t>(denied.size());
    if (!WriteAll(fd, &count, sizeof(count))) return false;
    for (const auto &pkg : denied) {
        uint32_t len = static_cast<uint32_t>(pkg.size());
        if (!WriteAll(fd, &len, sizeof(len))) return false;
        if (!WriteAll(fd, pkg.data(), len)) return false;
    }
    return true;
}

// Companion mode WATCH: kirim snapshot awal, lalu pantau kDenylistFile lewat
// inotify dan kirim ulang snapshot setiap kali file itu berubah. Berjalan
// selama fd masih hidup (yaitu selama system_server, yang memegang ujung
// lain socket ini, masih hidup).
//
// IN_CLOSE_WRITE dipakai (bukan IN_MODIFY) supaya reload cuma terjadi SEKALI
// setelah penulis benar-benar selesai menutup file descriptor-nya — baik
// saat WebUI menulis lewat heredoc (banyak write() kecil beruntun) maupun
// saat action.sh melakukan `cat > file` untuk trigger reload manual.
static constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

static void RunWatchMode(int fd) {
    if (!SendSnapshot(fd)) return;

    for (;;) {
        int ifd = inotify_init1(IN_CLOEXEC);
        if (ifd < 0) {
            LOGE("companion: inotify_init1 gagal (errno=%d, %s)", errno, strerror(errno));
            return;
        }

        int wd = inotify_add_watch(ifd, kDenylistFile, kWatchMask);
        if (wd < 0) {
            LOGW("companion: inotify_add_watch(%s) gagal (errno=%d, %s), coba lagi 5 detik lagi",
                 kDenylistFile, errno, strerror(errno));
            close(ifd);
            struct timespec ts{5, 0};
            nanosleep(&ts, nullptr);
            // fd socket ke client dicek tetap hidup secara implisit lewat
            // percobaan kirim snapshot berikutnya; kalau client sudah putus,
            // WriteAll di SendSnapshot akan gagal dan kita keluar total.
            if (!SendSnapshot(fd)) return;
            continue;
        }

        constexpr size_t kBufSize = 4096;
        alignas(struct inotify_event) char buf[kBufSize];
        bool watchBroken = false;

        while (!watchBroken) {
            ssize_t n = read(ifd, buf, kBufSize);
            if (n <= 0) {
                if (errno == EINTR) continue;
                LOGW("companion: baca inotify fd gagal (errno=%d, %s), pasang ulang watch",
                     errno, strerror(errno));
                break;
            }

            ssize_t offset = 0;
            bool shouldReload = false;
            bool inodeChanged = false;
            while (offset < n) {
                auto *ev = reinterpret_cast<struct inotify_event *>(buf + offset);
                if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                    inodeChanged = true;
                    shouldReload = true;
                } else if (ev->mask & IN_CLOSE_WRITE) {
                    shouldReload = true;
                }
                offset += static_cast<ssize_t>(sizeof(struct inotify_event)) + ev->len;
            }

            if (shouldReload) {
                if (!SendSnapshot(fd)) {
                    // Client (system_server) sudah putus koneksi — companion
                    // tidak ada gunanya lagi menunggu, keluar total.
                    close(ifd);
                    return;
                }
            }
            if (inodeChanged) watchBroken = true;
        }

        close(ifd);
        struct timespec ts{0, 200 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
}

// Companion process: jalan sebagai root, terpisah dari system_server.
// Bisa dipanggil berkali-kali secara PARALEL oleh Zygisk — sekali untuk
// koneksi snapshot (preServerSpecialize, selesai cepat) dan sekali lagi
// untuk koneksi watch (bertahan selama system_server hidup). Keduanya
// independen, tidak saling mengunci.
static void CompanionMain(int fd) {
    uint8_t trigger;
    if (!ReadAll(fd, &trigger, 1)) return;

    if (trigger == kTriggerWatch) {
        RunWatchMode(fd);
    } else {
        SendSnapshot(fd);
    }
}

REGISTER_ZYGISK_COMPANION(CompanionMain)

// ---------------------------------------------------------------------------
// Modul Zygisk. Kita cuma peduli sama system_server (pre/postServerSpecialize)
// karena hook aslinya cuma dipasang di proses itu.
// ---------------------------------------------------------------------------

class IsolPolicyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // Tidak melakukan apa-apa di proses app biasa — hook cuma relevan
        // untuk system_server.
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        // connectCompanion() cuma valid dipanggil di tahap ini (masih
        // privilege zygote, belum kena sandbox system_server). Karena itu
        // KEDUA koneksi (snapshot sekali-pakai + watch persisten) harus
        // dibuka di sini sekaligus, meskipun watcher-nya baru benar-benar
        // dipakai nanti di postServerSpecialize.

        // --- Koneksi 1: snapshot awal, dipakai langsung, ditutup segera ---
        int snapshotFd = api_->connectCompanion();
        if (snapshotFd < 0) {
            LOGE("connectCompanion() (snapshot) gagal, modul tidak akan aktif");
            return;
        }

        uint8_t triggerSnapshot = kTriggerSnapshot;
        if (!WriteAll(snapshotFd, &triggerSnapshot, 1)) {
            LOGE("gagal kirim trigger snapshot ke companion");
            close(snapshotFd);
            return;
        }

        uint32_t count = 0;
        if (!ReadAll(snapshotFd, &count, sizeof(count))) {
            LOGE("gagal baca jumlah entri denylist dari companion");
            close(snapshotFd);
            return;
        }

        pending_denylist_.clear();
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            if (!ReadAll(snapshotFd, &len, sizeof(len))) break;
            std::string pkg(len, '\0');
            if (!ReadAll(snapshotFd, pkg.data(), len)) break;
            pending_denylist_.insert(std::move(pkg));
        }
        close(snapshotFd);

        LOGI("ambil %zu paket denylist dari companion", pending_denylist_.size());
        ready_ = true;

        // --- Koneksi 2: watch persisten, TIDAK ditutup di sini ---
        // Kalau gagal, bukan fatal — modul tetap jalan pakai snapshot di
        // atas, cuma reload live tidak aktif (perlu reboot untuk perubahan
        // berikutnya). StartDenylistWatcher() menangani watcherFd_ == -1
        // dengan aman.
        watcherFd_ = api_->connectCompanion();
        if (watcherFd_ < 0) {
            LOGW("connectCompanion() (watch) gagal, reload live tidak aktif");
        } else {
            uint8_t triggerWatch = kTriggerWatch;
            if (!WriteAll(watcherFd_, &triggerWatch, 1)) {
                LOGW("gagal kirim trigger watch ke companion, reload live tidak aktif");
                close(watcherFd_);
                watcherFd_ = -1;
            }
        }
    }

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        if (!ready_) return;
        bool ok = InstallBindHook(env_, std::move(pending_denylist_));
        LOGI(ok ? "hook berhasil dipasang di system_server"
                : "gagal memasang hook di system_server");
        if (ok) {
            // Nyalakan thread yang memakai koneksi watch dari
            // preServerSpecialize: perubahan denylist lewat WebUI atau
            // action.sh (reload manual) langsung berlaku untuk fork proses
            // berikutnya, tanpa reboot / restart apapun.
            StartDenylistWatcher(watcherFd_);
        } else if (watcherFd_ >= 0) {
            close(watcherFd_);
        }
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool ready_ = false;
    int watcherFd_ = -1;
    std::unordered_set<std::string> pending_denylist_;
};

REGISTER_ZYGISK_MODULE(IsolPolicyModule)

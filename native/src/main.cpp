#include "zygisk.hpp"
#include "bind_hook.h"
#include "config_store.h"
#include "log.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <unordered_set>
#include <string>

using namespace isolpolicy;

// ---------------------------------------------------------------------------
// Protokol IPC sederhana ke companion root:
//   client -> companion : 1 byte apa saja (trigger)
//   companion -> client : uint32_t count, lalu per entri [uint32_t len][bytes]
// ---------------------------------------------------------------------------

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

// Companion process: jalan sebagai root, terpisah dari system_server.
static void CompanionMain(int fd) {
    uint8_t trigger;
    if (!ReadAll(fd, &trigger, 1)) return;

    std::unordered_set<std::string> denied = LoadDenylistFromDisk();

    uint32_t count = static_cast<uint32_t>(denied.size());
    if (!WriteAll(fd, &count, sizeof(count))) return;
    for (const auto &pkg : denied) {
        uint32_t len = static_cast<uint32_t>(pkg.size());
        if (!WriteAll(fd, &len, sizeof(len))) return;
        if (!WriteAll(fd, pkg.data(), len)) return;
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
        // privilege zygote, belum kena sandbox system_server).
        int fd = api_->connectCompanion();
        if (fd < 0) {
            LOGE("connectCompanion() gagal, modul tidak akan aktif");
            return;
        }

        uint8_t trigger = 1;
        if (!WriteAll(fd, &trigger, 1)) {
            LOGE("gagal kirim trigger ke companion");
            close(fd);
            return;
        }

        uint32_t count = 0;
        if (!ReadAll(fd, &count, sizeof(count))) {
            LOGE("gagal baca jumlah entri denylist dari companion");
            close(fd);
            return;
        }

        pending_denylist_.clear();
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            if (!ReadAll(fd, &len, sizeof(len))) break;
            std::string pkg(len, '\0');
            if (!ReadAll(fd, pkg.data(), len)) break;
            pending_denylist_.insert(std::move(pkg));
        }
        close(fd);

        LOGI("ambil %zu paket denylist dari companion", pending_denylist_.size());
        ready_ = true;
    }

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        if (!ready_) return;
        bool ok = InstallBindHook(env_, std::move(pending_denylist_));
        LOGI(ok ? "hook berhasil dipasang di system_server"
                : "gagal memasang hook di system_server");
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool ready_ = false;
    std::unordered_set<std::string> pending_denylist_;
};

REGISTER_ZYGISK_MODULE(IsolPolicyModule)

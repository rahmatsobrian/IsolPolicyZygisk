#include "denylist_watcher.h"
#include "bind_hook.h"
#include "log.h"

#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>

namespace isolpolicy {

// Protokol pada koneksi watcher (lihat juga CompanionWatchMain di main.cpp):
//   client -> companion : 1 byte, nilai kTriggerWatch (bukan kTriggerSnapshot)
//   companion -> client : berulang kali, setiap kali file config berubah:
//                          uint32_t count, lalu per entri [uint32_t len][bytes]
// Socket ini TETAP TERBUKA selama system_server hidup; companion mem-push
// snapshot baru setiap kali inotify-nya mendeteksi file berubah, kapan pun
// itu terjadi — client (thread ini) cuma perlu loop baca terus-menerus.

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

static void WatcherLoop(int fd) {
    LOGI("denylist watcher terhubung ke companion (fd=%d), menunggu update...", fd);
    for (;;) {
        uint32_t count = 0;
        if (!ReadAll(fd, &count, sizeof(count))) {
            LOGW("koneksi watcher ke companion terputus, reload live berhenti "
                 "(hook tetap jalan dengan denylist terakhir yang diketahui)");
            break;
        }

        std::unordered_set<std::string> denylist;
        denylist.reserve(count);
        bool ok = true;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            if (!ReadAll(fd, &len, sizeof(len))) { ok = false; break; }
            std::string pkg(len, '\0');
            if (!ReadAll(fd, pkg.data(), len)) { ok = false; break; }
            denylist.insert(std::move(pkg));
        }
        if (!ok) {
            LOGW("data push dari companion terpotong, koneksi watcher ditutup");
            break;
        }

        LOGI("denylist reload live: %zu paket", denylist.size());
        ReplaceDenylist(std::move(denylist));
    }
    close(fd);
}

void StartDenylistWatcher(int watcherFd) {
    if (watcherFd < 0) {
        LOGW("koneksi watcher tidak tersedia, reload live dinonaktifkan "
             "(perubahan denylist baru berlaku setelah reboot / restart proses)");
        return;
    }
    // detach: thread ini hidup selama system_server hidup / selama koneksi
    // ke companion masih ada, tidak pernah di-join secara eksplisit.
    std::thread(WatcherLoop, watcherFd).detach();
}

} // namespace isolpolicy

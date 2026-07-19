#include "config_store.h"
#include "log.h"

#include <fstream>
#include <sstream>

namespace isolpolicy {

static std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::unordered_set<std::string> LoadDenylistFromDisk() {
    std::unordered_set<std::string> out;
    out.insert(kSelfPackage); // paket sendiri selalu di-deny, sama seperti versi Xposed

    std::ifstream f(kDenylistFile);
    if (!f.is_open()) {
        LOGW("denylist file %s not found, using defaults only", kDenylistFile);
        return out;
    }

    std::string line;
    while (std::getline(f, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        out.insert(t);
    }
    LOGI("loaded %zu denied package(s) from disk", out.size());
    return out;
}

} // namespace isolpolicy

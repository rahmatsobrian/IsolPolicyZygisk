#include "art_symbol.h"
#include "log.h"

#include <dlfcn.h>
#include <string>

namespace isolpolicy {

static void *g_art_handle = nullptr;

bool OpenArtHandle() {
    // libart.so sudah ada di address space (system_server jelas pakai ART),
    // RTLD_NOLOAD supaya cuma ambil handle, tidak bikin mapping baru.
    g_art_handle = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_art_handle) {
        LOGE("dlopen(libart.so, RTLD_NOLOAD) failed: %s", dlerror());
        return false;
    }
    return true;
}

void *ResolveArtSymbol(std::string_view name) {
    if (!g_art_handle) return nullptr;
    // dlsym cukup untuk kebanyakan simbol yang lsplant butuhkan pada
    // sebagian besar build AOSP/vendor. Kalau ada simbol yang missing di
    // ROM tertentu (biasa kena hidden-symbol restriction), ini titik yang
    // perlu diganti jadi parser .dynsym/.symtab manual (mis. pakai
    // libelf-lite) — lihat catatan di README.
    void *sym = dlsym(g_art_handle, std::string(name).c_str());
    if (!sym) {
        LOGW("ART symbol not found via dlsym: %.*s",
             (int) name.size(), name.data());
    }
    return sym;
}

} // namespace isolpolicy

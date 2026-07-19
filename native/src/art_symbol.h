#pragma once
#include <string_view>

namespace isolpolicy {

// Buka handle ke libart.so yang sudah di-load di proses (system_server
// selalu punya libart.so di memory), dipakai sebagai art_symbol_resolver
// untuk lsplant::Init(). Pakai RTLD_NOLOAD karena kita tidak mau load ulang
// / bikin instance baru, cuma ambil handle ke yang sudah ada.
bool OpenArtHandle();
void *ResolveArtSymbol(std::string_view name);

} // namespace isolpolicy

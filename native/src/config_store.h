#pragma once
#include <string>
#include <unordered_set>

namespace isolpolicy {

// Direktori data persisten modul (dibuat oleh customize.sh / service.sh),
// TIDAK ada di dalam folder modul Magisk supaya tidak ikut kehapus saat
// modul di-update lewat Manager.
constexpr const char *kDataDir = "/data/adm/isolpolicy";
constexpr const char *kDenylistFile = "/data/adm/isolpolicy/denylist.conf";
constexpr const char *kSelfPackage = "io.github.mhmrdd.isolationpolicy";

// Format file: satu package name per baris, baris kosong / diawali '#'
// diabaikan. File ini ditulis oleh WebUI (lewat exec shell dengan hak root
// dari Manager), dibaca oleh proses companion root Zygisk (bukan langsung
// oleh system_server, supaya tidak kena batasan SELinux baca file arbitrer).
std::unordered_set<std::string> LoadDenylistFromDisk();

} // namespace isolpolicy

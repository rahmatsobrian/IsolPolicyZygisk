#pragma once
#include <jni.h>
#include <unordered_set>
#include <string>

namespace isolpolicy {

// Dipanggil sekali di postServerSpecialize, dengan snapshot denylist yang
// sudah diambil dari companion root di preServerSpecialize.
// Return false kalau init LSPlant / pasang hook gagal (dicatat ke logcat,
// modul tidak melakukan apa-apa lagi setelah itu — fail-safe, tidak crash
// system_server).
bool InstallBindHook(JNIEnv *env, std::unordered_set<std::string> denylist);

// Dipanggil dari native method ProcessListHooker.isDenied(String) di sisi
// Java lewat RegisterNatives.
bool IsPackageDenied(const std::string &pkg);

// Mengganti seluruh isi denylist yang sedang aktif dengan snapshot baru,
// secara thread-safe (dipakai oleh denylist_watcher.cpp saat file config
// berubah, supaya perubahan lewat WebUI langsung berlaku tanpa reboot atau
// restart proses apapun).
void ReplaceDenylist(std::unordered_set<std::string> denylist);

} // namespace isolpolicy

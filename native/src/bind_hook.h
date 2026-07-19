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

} // namespace isolpolicy

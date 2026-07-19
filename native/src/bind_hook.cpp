#include "bind_hook.h"
#include "art_symbol.h"
#include "config_store.h"
#include "hooker_dex.h"
#include "log.h"

#include <lsplant.hpp>
#include <dobby.h>

#include <mutex>
#include <vector>

namespace isolpolicy {

static std::mutex g_denylist_mutex;
static std::unordered_set<std::string> g_denylist;

bool IsPackageDenied(const std::string &pkg) {
    std::lock_guard<std::mutex> lock(g_denylist_mutex);
    return g_denylist.count(pkg) != 0;
}

// ---- native methods yang di-bind ke kelas Java ProcessListHooker ----------

static jboolean native_isDenied(JNIEnv *env, jclass, jstring jpkg) {
    if (!jpkg) return JNI_FALSE;
    const char *chars = env->GetStringUTFChars(jpkg, nullptr);
    bool denied = chars && IsPackageDenied(chars);
    if (chars) env->ReleaseStringUTFChars(jpkg, chars);
    return denied ? JNI_TRUE : JNI_FALSE;
}

static void native_logInfo(JNIEnv *env, jclass, jstring jmsg) {
    if (!jmsg) return;
    const char *chars = env->GetStringUTFChars(jmsg, nullptr);
    if (chars) LOGI("%s", chars);
    env->ReleaseStringUTFChars(jmsg, chars);
}

// ---- helper: Dobby sebagai inline hook backend buat lsplant ---------------

static void *DobbyInlineHook(void *target, void *hooker) {
    void *backup = nullptr;
    if (DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(hooker),
                  reinterpret_cast<dobby_dummy_func_t *>(&backup)) != 0) {
        return nullptr;
    }
    return backup;
}

static bool DobbyInlineUnhook(void *func) {
    return DobbyDestroy(func) == 0;
}

// ---- load kelas ProcessListHooker via InMemoryDexClassLoader --------------

static jclass LoadHookerClass(JNIEnv *env, jobject targetClassLoader) {
    if (kHookerDexLen == 0) {
        LOGE("hooker_dex.h masih placeholder kosong -> tidak ada dex untuk di-load. "
             "Pastikan job build-hooker-dex sudah jalan sebelum compile native.");
        return nullptr;
    }

    jobject byteBuffer = env->NewDirectByteBuffer(const_cast<unsigned char *>(kHookerDex),
                                                   kHookerDexLen);
    if (!byteBuffer) {
        LOGE("NewDirectByteBuffer gagal");
        return nullptr;
    }

    jclass loaderClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!loaderClass) {
        LOGE("FindClass(InMemoryDexClassLoader) gagal");
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(loaderClass, "<init>",
                                       "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject loaderInstance = env->NewObject(loaderClass, ctor, byteBuffer, targetClassLoader);
    if (!loaderInstance) {
        LOGE("konstruksi InMemoryDexClassLoader gagal");
        return nullptr;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassMethod = env->GetMethodID(
        classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    jstring className = env->NewStringUTF(
        "io.github.mhmrdd.isolationpolicy.hook.ProcessListHooker");
    auto hookerClass = (jclass) env->CallObjectMethod(loaderInstance, loadClassMethod, className);
    if (!hookerClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("loadClass(ProcessListHooker) gagal");
        return nullptr;
    }
    return hookerClass;
}

// ---- enumerasi overload startProcessLocked & pasang hook per method -------

static bool HookAllStartProcessLocked(JNIEnv *env, jclass processListClass, jclass hookerClass) {
    jmethodID getDeclaredMethods = env->GetMethodID(
        env->FindClass("java/lang/Class"), "getDeclaredMethods",
        "()[Ljava/lang/reflect/Method;");
    auto methods = (jobjectArray) env->CallObjectMethod(processListClass, getDeclaredMethods);
    if (!methods) {
        LOGE("getDeclaredMethods() di ProcessList gagal / null");
        return false;
    }

    jclass methodClass = env->FindClass("java/lang/reflect/Method");
    jmethodID getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
    jmethodID getReturnType = env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");

    jmethodID callbackMethod = env->GetMethodID(hookerClass, "callback",
                                                  "([Ljava/lang/Object;)Ljava/lang/Object;");
    jfieldID backupField = env->GetFieldID(hookerClass, "backupMethod",
                                            "Ljava/lang/reflect/Method;");
    jmethodID hookerCtor = env->GetMethodID(hookerClass, "<init>", "()V");

    jclass booleanPrimitive = env->FindClass("java/lang/Boolean");
    // Boolean.TYPE (primitive boolean.class) via reflection field
    jfieldID typeField = env->GetStaticFieldID(booleanPrimitive, "TYPE", "Ljava/lang/Class;");
    auto booleanType = (jclass) env->GetStaticObjectField(booleanPrimitive, typeField);

    jsize count = env->GetArrayLength(methods);
    int hookedCount = 0;

    for (jsize i = 0; i < count; i++) {
        jobject method = env->GetObjectArrayElement(methods, i);
        auto nameStr = (jstring) env->CallObjectMethod(method, getName);
        const char *nameChars = env->GetStringUTFChars(nameStr, nullptr);
        bool nameMatches = nameChars && std::string(nameChars) == "startProcessLocked";
        if (nameChars) env->ReleaseStringUTFChars(nameStr, nameChars);
        if (!nameMatches) continue;

        auto returnType = (jclass) env->CallObjectMethod(method, getReturnType);
        if (!env->IsSameObject(returnType, booleanType)) continue;

        jobject hookerInstance = env->NewObject(hookerClass, hookerCtor);
        jobject backup = lsplant::Hook(env, method, hookerInstance, callbackMethod);
        if (!backup) {
            LOGW("lsplant::Hook gagal untuk overload startProcessLocked ke-%d", (int) i);
            continue;
        }
        env->SetObjectField(hookerInstance, backupField, backup);
        // jaga referensi supaya tidak di-GC selama proses hidup
        env->NewGlobalRef(hookerInstance);
        hookedCount++;
    }

    LOGI("terpasang %d hook pada overload startProcessLocked", hookedCount);
    return hookedCount > 0;
}

bool InstallBindHook(JNIEnv *env, std::unordered_set<std::string> denylist) {
    {
        std::lock_guard<std::mutex> lock(g_denylist_mutex);
        g_denylist = std::move(denylist);
    }

    if (!OpenArtHandle()) return false;

    lsplant::InitInfo initInfo{
        .inline_hooker = DobbyInlineHook,
        .inline_unhooker = DobbyInlineUnhook,
        .art_symbol_resolver = [](std::string_view name) { return ResolveArtSymbol(name); },
    };
    if (!lsplant::Init(env, initInfo)) {
        LOGE("lsplant::Init gagal");
        return false;
    }

    jclass processListClass = env->FindClass("com/android/server/am/ProcessList");
    if (!processListClass) {
        env->ExceptionClear();
        LOGE("FindClass(ProcessList) gagal — apakah ini benar system_server?");
        return false;
    }

    // classloader system_server dipakai sebagai parent InMemoryDexClassLoader
    jmethodID getClassLoader = env->GetMethodID(
        env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject systemClassLoader = env->CallObjectMethod(processListClass, getClassLoader);

    jclass hookerClass = LoadHookerClass(env, systemClassLoader);
    if (!hookerClass) return false;

    // Native glue: ProcessListHooker.isDenied(String) / logInfo(String)
    JNINativeMethod natives[] = {
        {"isDenied", "(Ljava/lang/String;)Z", (void *) native_isDenied},
        {"logInfo", "(Ljava/lang/String;)V", (void *) native_logInfo},
    };
    if (env->RegisterNatives(hookerClass, natives, 2) != JNI_OK) {
        env->ExceptionClear();
        LOGE("RegisterNatives(ProcessListHooker) gagal");
        return false;
    }

    return HookAllStartProcessLocked(env, processListClass, hookerClass);
}

} // namespace isolpolicy

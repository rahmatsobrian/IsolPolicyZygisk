package io.github.mhmrdd.isolationpolicy.hook;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;

/**
 * Kelas kecil ini adalah pengganti BindHook.java versi Xposed.
 * Di-compile jadi DEX oleh CI (lihat .github/workflows/build.yml, job
 * build-hooker-dex), di-embed sebagai byte array ke dalam .so, dan di-load
 * runtime lewat dalvik.system.InMemoryDexClassLoader — tidak butuh APK.
 *
 * lsplant::Hook() akan mengganti isi method target (startProcessLocked)
 * supaya panggilannya diarahkan ke {@link #callback(Object[])} di sini,
 * dengan objek instance kelas ini sebagai context ("hooker_object").
 */
public class ProcessListHooker {

    // Diisi secara native (JNI SetObjectField) tepat setelah lsplant::Hook()
    // berhasil — ini method original sebelum di-hook, dipanggil balik untuk
    // kasus non-denylist supaya perilaku asli ProcessList tidak berubah.
    public Method backupMethod;

    // Dua method native ini di-bind lewat env->RegisterNatives di
    // bind_hook.cpp, langsung ke logika C++ yang baca denylist hasil
    // companion root.
    private static native boolean isDenied(String pkg);
    private static native void logInfo(String msg);

    public Object callback(Object[] args) throws Throwable {
        try {
            Object shortCircuit = maybeBlock(args);
            if (shortCircuit != null) return shortCircuit;
        } catch (Throwable t) {
            logInfo("callback error, fallback ke original: " + t);
        }
        return invokeOriginal(args);
    }

    /**
     * Logika ini sengaja dibuat semirip mungkin dengan BindHook.java versi
     * Xposed yang asli: cari HostingRecord & ProcessRecord di antara args,
     * cek usesAppZygote(), cek nama paket ada di denylist atau tidak.
     */
    private Object maybeBlock(Object[] args) {
        Object hostingRecord = null;
        Object processRecord = null;
        for (Object arg : args) {
            if (arg == null) continue;
            String cn = arg.getClass().getName();
            if (hostingRecord == null && cn.equals("com.android.server.am.HostingRecord")) {
                hostingRecord = arg;
            } else if (processRecord == null && cn.equals("com.android.server.am.ProcessRecord")) {
                processRecord = arg;
            }
        }
        if (hostingRecord == null || processRecord == null) return null;
        if (!usesAppZygote(hostingRecord)) return null;

        String pkg = resolvePackage(processRecord);
        if (pkg == null || !isDenied(pkg)) return null;

        logInfo("blokir fork app_zygote untuk " + pkg);
        return Boolean.TRUE;
    }

    private Object invokeOriginal(Object[] args) throws Throwable {
        if (backupMethod == null) {
            // Tidak seharusnya terjadi (berarti lsplant::Hook gagal set backup),
            // fail-open supaya tidak mematikan system_server.
            return Boolean.FALSE;
        }
        try {
            if (Modifier.isStatic(backupMethod.getModifiers())) {
                return backupMethod.invoke(null, args);
            }
            Object receiver = args[0];
            Object[] rest = Arrays.copyOfRange(args, 1, args.length);
            return backupMethod.invoke(receiver, rest);
        } catch (InvocationTargetException e) {
            throw e.getCause() != null ? e.getCause() : e;
        }
    }

    private static boolean usesAppZygote(Object hostingRecord) {
        try {
            Method m = hostingRecord.getClass().getMethod("usesAppZygote");
            Object r = m.invoke(hostingRecord);
            if (r instanceof Boolean) return (Boolean) r;
        } catch (Throwable ignored) {
        }
        try {
            java.lang.reflect.Field f = hostingRecord.getClass().getDeclaredField("mHostingZygote");
            f.setAccessible(true);
            return f.getInt(hostingRecord) == 2;
        } catch (Throwable ignored) {
        }
        return false;
    }

    private static String resolvePackage(Object processRecord) {
        try {
            java.lang.reflect.Field infoField = processRecord.getClass().getDeclaredField("info");
            infoField.setAccessible(true);
            Object info = infoField.get(processRecord);
            if (info == null) return null;
            java.lang.reflect.Field pkgField = info.getClass().getDeclaredField("packageName");
            pkgField.setAccessible(true);
            Object pkg = pkgField.get(info);
            if (pkg instanceof String && !((String) pkg).isEmpty()) return (String) pkg;
        } catch (Throwable ignored) {
        }
        return null;
    }
}

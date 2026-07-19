# IsolPolicy — Zygisk Edition (standalone, no LSPosed/Xposed)

Port dari modul LSPosed `IsolPolicy` (hook `ProcessList#startProcessLocked` di
`system_server` untuk memblokir jalur `useAppZygote` pada paket yang dipilih)
menjadi **modul Magisk/KernelSU/APatch berbasis Zygisk murni**, tanpa
dependensi ke LSPosed sama sekali. Kontrol daftar deny dilakukan lewat
**WebUI** (KernelSU Manager / APatch / MMRL-compatible), bukan lewat Activity
Java seperti versi asli.

## Kenapa arsitekturnya berbeda dari versi Xposed

Xposed/LSPosed menyediakan `XposedBridge.hookAllMethods()` yang di baliknya
memakai hooking ART tingkat tinggi + `XSharedPreferences` untuk baca
konfigurasi lintas-proses. Zygisk **tidak** menyediakan hook Java sama
sekali secara default — dia cuma kasih:

- `preAppSpecialize` / `postAppSpecialize` — sebelum & sesudah app process
  di-spesialisasi.
- `preServerSpecialize` / `postServerSpecialize` — versi khusus untuk
  `system_server` (ini yang kita pakai, karena target hook memang di situ).
- `connectCompanion()` — IPC ke proses companion root, **cuma bisa dipanggil
  di tahap pre-Specialize**.
- `hookJniNativeMethods` / `pltHookRegister` — buat hook native/JNI, bukan
  hook method Java murni.

Karena target kita (`ProcessList.startProcessLocked`) adalah method Java biasa
di `system_server`, kita tetap butuh mesin hook ART. Solusinya: pakai
**[LSPlant](https://github.com/LSPosed/LSPlant)** — library C++ yang jadi
inti hooking ART di LSPosed generasi baru sendiri, tapi bisa dipakai berdiri
sendiri lewat Zygisk tanpa perlu install LSPosed. LSPlant butuh backend
inline-hook (dipasangkan dengan **Dobby**) untuk hook fungsi native ART di
level trampoline.

Karena callback hook LSPlant harus berupa **method Java** (bukan pointer C++
langsung), kita sisipkan satu kelas Java kecil (`ProcessListHooker`) yang berisi
ulang logika `BindHook.java` yang asli (cek `HostingRecord`/`ProcessRecord`,
cek `usesAppZygote()`, cek denylist, balikin `Boolean.TRUE` buat skip fork).
Kelas ini di-compile jadi DEX kecil oleh CI, di-embed sebagai byte array ke
dalam `.so`, lalu di-load runtime lewat
`dalvik.system.InMemoryDexClassLoader` — tidak perlu APK, tidak perlu
LSPosed, cuma satu file `.so` per-arch di folder `zygisk/` modul Magisk.

## Perbedaan perilaku vs versi asli

| | Versi LSPosed (asli) | Versi Zygisk (ini) |
|---|---|---|
| Kontrol UI | Activity di dalam APK | WebUI (HTML/JS) |
| Simpan config | `XSharedPreferences` (world-readable prefs) | file teks `/data/adm/isolpolicy/denylist.conf`, dibaca companion root saat boot |
| Apply perubahan | Langsung (baca ulang prefs tiap panggilan) | **Butuh reboot** — daftar deny dibaca sekali saat `system_server` fork, karena `connectCompanion()` cuma valid di pre-specialize |
| Dependensi | LSPosed/EdXposed terpasang | Cuma Magisk/KernelSU/APatch dengan Zygisk aktif |

Trade-off "butuh reboot" ini disengaja — ini pola umum untuk modul Zygisk yang
nge-hook `system_server`, karena proses itu cuma hidup sekali per boot dan
kanal companion resmi memang dibatasi SELinux di luar pre-specialize.

## Struktur proyek

```
IsolPolicyZygisk/
├── native/
│   ├── src/                     # kode C++ modul zygisk
│   │   ├── zygisk.hpp           # header resmi topjohnwu, jangan diubah
│   │   ├── main.cpp             # entry point ModuleBase + companion
│   │   ├── bind_hook.cpp/.h     # setup LSPlant + install hook
│   │   ├── art_symbol.cpp/.h    # resolver simbol libart.so
│   │   ├── config_store.cpp/.h  # baca denylist.conf
│   │   └── log.h
│   ├── hooker_java/             # source Java kecil (dicompile jadi dex oleh CI)
│   │   └── .../ProcessListHooker.java
│   └── CMakeLists.txt
├── magisk_module/
│   ├── module.prop
│   ├── customize.sh
│   ├── uninstall.sh
│   ├── webroot/                 # WebUI (index.html/app.js/style.css)
│   └── zygisk/                  # diisi CI: arm64-v8a.so, armeabi-v7a.so
└── .github/workflows/build.yml  # build NDK + d8 + assemble zip flashable
```

## Build

Semuanya dibuild di GitHub Actions (`.github/workflows/build.yml`), sama
seperti workflow ServerHive/CI yang biasa dipakai:

1. Job `build-hooker-dex`: compile `ProcessListHooker.java` pakai `javac`
   lalu convert ke DEX pakai `d8` (dari Android cmdline-tools), hasilnya
   di-`xxd -i` jadi header C `hooker_dex.h`.
2. Job `build-native`: compile `.so` per-ABI pakai Android NDK (r27+),
   `lsplant` & `Dobby` di-fetch via CMake `FetchContent` dari GitHub.
3. Job `package`: susun `magisk_module/` + `.so` hasil build jadi zip
   flashable `IsolPolicyZygisk-vX.zip`.

Tidak bisa saya compile langsung di sandbox chat ini karena tidak ada Android
NDK/SDK terpasang di sini (jaringan sandbox cuma boleh akses github/pypi/npm,
bukan `dl.google.com` tempat NDK di-hosting) — makanya semua step build saya
taruh di GitHub Actions, persis kayak workflow K4.19/VoltageOS lo yang lain.

## Install

1. Ambil zip hasil Actions artifact/release.
2. Flash lewat Magisk/KernelSU/APatch Manager seperti modul biasa.
3. **Reboot** (wajib, karena hook dipasang saat `system_server` fork).
4. Buka WebUI modul dari Manager (ikon WebUI) buat pilih paket yang mau
   di-deny dari `useAppZygote`.
5. Reboot lagi tiap habis ubah daftar deny.

## Yang masih perlu lo verifikasi di device asli

- Nama-nama simbol ART yang dipakai `art_symbol_resolver` (`art_symbol.cpp`)
  bisa beda antar versi Android/ROM (khususnya AOSP vs vendor fork kayak
  MIUI/HyperOS). Kalau hook gagal pasang, cek logcat tag `IsolPolicyZ`.
- Path `/data/adm/isolpolicy/denylist.conf` — pastikan `service.sh`/
  `customize.sh` bikin direktori itu dengan permission yang bisa dibaca
  companion root (harusnya otomatis karena companion jalan sebagai root,
  tapi context SELinux file tetap perlu dicek kalau device pakai policy
  ketat non-AOSP).
- `startProcessLocked` bisa saja beda tanda tangan/nama di Android versi
  yang lebih baru — kode saya enumerasi semua overload dengan nama itu &
  return type `boolean`, sama seperti `hookAllMethods` versi Xposed, jadi
  seharusnya tetap match, tapi tetap perlu dites di ROM target lo
  (vince/MSM8937 base Android berapa?).

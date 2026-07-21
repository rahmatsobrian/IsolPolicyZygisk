#pragma once

namespace isolpolicy {

// Menyalakan thread background yang memakai KONEKSI PERSISTEN kedua ke
// companion root (fd yang SUDAH terbuka, lihat catatan di bawah), lalu
// menunggu companion mem-push ulang denylist setiap kali kDenylistFile
// (lihat config_store.h) berubah.
//
// PENTING soal SELinux: watcher inotify berjalan di SISI COMPANION (proses
// root terpisah), BUKAN langsung di system_server — karena system_server
// terikat kebijakan SELinux domain system_server yang tidak mengizinkan
// membaca file arbitrer di /data/adm/..., sementara companion Zygisk
// berjalan sebagai root penuh di luar sandbox itu. system_server hanya
// menerima data yang sudah dibaca oleh companion lewat socket.
//
// PENTING soal timing: zygisk::Api::connectCompanion() HANYA valid
// dipanggil dari pre[XXX]Specialize (dibatasi SELinux, lihat komentar di
// zygisk.hpp). Karena itu fd koneksi watcher ini HARUS sudah dibuat di
// preServerSpecialize (connect kedua, terpisah dari koneksi snapshot awal
// yang langsung ditutup), lalu diserahkan ke fungsi ini yang baru
// benar-benar dipanggil setelah itu (dari postServerSpecialize). Fungsi ini
// sendiri tidak memanggil connectCompanion() sama sekali.
//
// watcherFd berpindah kepemilikan ke thread watcher (akan ditutup otomatis
// kalau companion putus koneksi / proses berhenti). Kalau watcherFd < 0
// (artinya koneksi kedua gagal dibuka), fungsi ini hanya mencatat log dan
// modul tetap jalan dengan snapshot denylist awal (fail-safe, tidak fatal).
void StartDenylistWatcher(int watcherFd);

} // namespace isolpolicy

#!/system/bin/sh
# shellcheck shell=dash
#
# Dijalankan saat user menekan tombol "Action" pada baris modul ini di
# Magisk Manager / KernelSU Manager / APatch Manager.
#
# Modul ini sudah reload otomatis lewat inotify (watcher di companion root
# process, lihat native/src/main.cpp RunWatchMode) setiap kali
# denylist.conf ditulis ulang dari WebUI — jadi TIDAK PERLU tombol ini
# untuk pemakaian normal sehari-hari.
#
# Tombol ini disediakan sebagai jalur cadangan untuk dua kasus:
#   1. Watcher entah kenapa berhenti (mis. companion daemon sempat mati,
#      koneksi socket putus) dan reload otomatis berhenti bekerja.
#   2. denylist.conf diedit manual lewat shell/file manager lain (bukan
#      lewat WebUI), yang mungkin tidak selalu memicu IN_CLOSE_WRITE
#      tergantung tool yang dipakai untuk mengeditnya.
#
# Cara kerja: "colek" file dengan menulis ulang isinya persis sama
# (cat file > file). Ini memicu event IN_CLOSE_WRITE di sisi companion,
# yang otomatis membaca ulang isi file dan mendorong (push) snapshot baru
# ke system_server lewat socket watch yang sudah terbuka — tanpa reboot,
# tanpa restart proses apapun.

DATA_DIR="/data/adm/isolpolicy"
DENYLIST_FILE="$DATA_DIR/denylist.conf"

if [ ! -f "$DENYLIST_FILE" ]; then
  echo "! $DENYLIST_FILE tidak ditemukan"
  echo "! Buka WebUI modul ini dulu untuk membuat konfigurasi awal"
  exit 1
fi

TMP_FILE="$DATA_DIR/.denylist.reload.tmp"
if cat "$DENYLIST_FILE" > "$TMP_FILE" 2>/dev/null && mv "$TMP_FILE" "$DENYLIST_FILE"; then
  chmod 644 "$DENYLIST_FILE"
  echo "- Reload dipicu."
  echo "- Cek logcat (tag IsolPolicyZ) untuk konfirmasi:"
  echo "    adb logcat -s IsolPolicyZ"
  echo "  Baris \"denylist reload live: N paket\" berarti berhasil."
else
  rm -f "$TMP_FILE" 2>/dev/null
  echo "! Gagal menulis ulang $DENYLIST_FILE"
  echo "! Cek: apakah /data/adm masih ter-mount rw?"
  exit 1
fi

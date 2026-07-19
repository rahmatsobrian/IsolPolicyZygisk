#!/system/bin/sh
# shellcheck shell=dash
SKIPUNZIP=0

ui_print "- IsolPolicy (Zygisk edition)"
ui_print "- Modul standalone, tidak butuh LSPosed/Xposed"

# Cek Zygisk aktif. Deteksi kasar tapi cukup untuk Magisk/KernelSU/APatch:
if [ -e "/data/adm/.magisk/modules" ] || [ -d "/data/adm" ]; then
  : # lanjut, tidak semua manager expose properti yang sama
fi

ZYGISK_ENABLED=$(getprop persist.zygisk 2>/dev/null)
if [ "$ZYGISK_ENABLED" = "0" ]; then
  ui_print "! Properti persist.zygisk = 0"
  ui_print "! Aktifkan Zygisk dulu di Manager, lalu reboot & pasang ulang modul ini"
fi

# Direktori data persisten (di luar folder modul, supaya tidak kehapus saat
# modul di-update lewat Manager).
DATA_DIR="/data/adm/isolpolicy"
mkdir -p "$DATA_DIR"
if [ ! -f "$DATA_DIR/denylist.conf" ]; then
  cat > "$DATA_DIR/denylist.conf" <<'EOF'
# Satu nama paket per baris. Baris kosong / diawali '#' diabaikan.
# Paket modul ini sendiri (io.github.mhmrdd.isolationpolicy) SELALU
# di-deny secara internal, tidak perlu ditulis di sini.
EOF
fi
chmod 644 "$DATA_DIR/denylist.conf"
chmod 755 "$DATA_DIR"

ui_print "- Konfigurasi awal dibuat di $DATA_DIR"
ui_print "- Buka WebUI modul ini dari Manager buat pilih paket yang di-deny"
ui_print "- WAJIB reboot setelah pasang, dan setiap kali ubah daftar deny"

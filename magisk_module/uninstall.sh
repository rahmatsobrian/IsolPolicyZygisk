#!/system/bin/sh
# Modul ini sengaja TIDAK menghapus /data/adm/isolpolicy/denylist.conf saat
# uninstall, supaya kalau modul dipasang ulang, daftar deny yang sudah
# diatur user tidak hilang. Hapus manual kalau memang mau bersih total:
#   rm -rf /data/adm/isolpolicy

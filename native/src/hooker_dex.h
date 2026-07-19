// GENERATED FILE — placeholder only.
//
// File asli dibuat otomatis oleh job `build-hooker-dex` di
// .github/workflows/build.yml, dengan urutan:
//   javac ProcessListHooker.java -> ProcessListHooker.class
//   d8 ProcessListHooker.class --output . -> classes.dex
//   xxd -i classes.dex > hooker_dex.h   (lalu rename array & panjangnya)
//
// JANGAN build native module ini memakai placeholder di bawah — isinya
// kosong dan hook tidak akan pernah ter-pasang. CI akan meng-overwrite file
// ini sebelum step compile native berjalan.

#pragma once

extern const unsigned char kHookerDex[];
extern const unsigned int kHookerDexLen;

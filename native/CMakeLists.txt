cmake_minimum_required(VERSION 3.28)
project(isolpolicy_zygisk)

# LSPlant butuh C++23 + Clang module support -> pakai NDK r27 ke atas.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# LSPlant — mesin hook ART (dipakai LSPosed generasi baru sendiri).
# Kita pakai CMakeLists standalone yang sudah disediakan di dalam repo di
# lsplant/src/main/jni (target: lsplant_static).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    lsplant_src
    GIT_REPOSITORY https://github.com/LSPosed/LSPlant.git
    GIT_TAG        master
    # LSPlant juga punya submodule lain di test/ yang pakai URL SSH
    # (git@github.com:...) dan gagal di-clone di CI (tidak ada SSH key).
    # Kita cuma butuh dex_builder, jadi batasi submodule yang di-init.
    GIT_SUBMODULES "lsplant/src/main/jni/external/dex_builder"
)
FetchContent_Populate(lsplant_src)
set(LSPLANT_STANDALONE ON CACHE INTERNAL "")
add_subdirectory(${lsplant_src_SOURCE_DIR}/lsplant/src/main/jni
                 ${CMAKE_BINARY_DIR}/lsplant_build)

# ---------------------------------------------------------------------------
# Dobby — backend inline-hook untuk lsplant::InitInfo::inline_hooker.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    dobby_src
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG        master
)
set(DOBBY_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(dobby_src)

# ---------------------------------------------------------------------------
# hooker_dex.h — DIGENERATE OLEH CI (lihat .github/workflows/build.yml,
# job build-hooker-dex). Kalau file di src/hooker_dex.h masih placeholder
# kosong, build ini akan tetap sukses tapi modul tidak akan melakukan apa-apa
# saat runtime (hook tidak pernah kepasang karena dex kosong).
# ---------------------------------------------------------------------------

add_library(isolpolicy SHARED
    src/main.cpp
    src/bind_hook.cpp
    src/art_symbol.cpp
    src/config_store.cpp
)

target_include_directories(isolpolicy PRIVATE src)

target_link_libraries(isolpolicy PRIVATE
    lsplant_static
    dobby_static
    log
    android
)

# Modul Zygisk WAJIB bernama sesuai ABI, ini diatur lagi di CI saat
# menyalin hasil build ke magisk_module/zygisk/<abi>.so
set_target_properties(isolpolicy PROPERTIES
    OUTPUT_NAME "isolpolicy"
)

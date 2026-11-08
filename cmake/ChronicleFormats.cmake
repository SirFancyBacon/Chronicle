# Chronicle's own image libraries: zstd, which baked KTX2 textures are compressed with (the game reads them, dcdata writes
# them), and stb_image_write, with which dcdata exports them back to PNG. Both the game's and dcdata's CMake include this.
# scripts/port/flatpak_sources.py checks the Flatpak manifest stages each download here, as it does Kazarov's.
include_guard(GLOBAL)
include(FetchContent)
enable_language(C)

# Takes the file from the first KZ_PORT_VENDOR_DIRS entry that holds it, as Kazarov's _kz_port_vendor does, and otherwise
# fetches it at its pinned hash. The Flatpak build has no network and stages it there.
function(chronicle_vendor out_dir name url sha256)
    foreach(dir IN LISTS KZ_PORT_VENDOR_DIRS)
        if(EXISTS "${dir}/${name}")
            set(${out_dir} "${dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(dir "${CMAKE_BINARY_DIR}/chronicle/vendor")
    file(DOWNLOAD ${url} "${dir}/${name}" EXPECTED_HASH SHA256=${sha256})
    set(${out_dir} "${dir}" PARENT_SCOPE)
endfunction()

# zstd's own CMake installs its library and headers whatever is asked, so only its sources are fetched (no CMakeLists.txt
# lies in SOURCE_SUBDIR) and built here. Without its x86-64 assembly, so that every toolchain builds it the same.
FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR sources-only)
FetchContent_MakeAvailable(zstd)
if(NOT TARGET chronicle_zstd)
    file(GLOB chronicle_zstd_sources CONFIGURE_DEPENDS
        ${zstd_SOURCE_DIR}/lib/common/*.c ${zstd_SOURCE_DIR}/lib/compress/*.c ${zstd_SOURCE_DIR}/lib/decompress/*.c)
    add_library(chronicle_zstd STATIC ${chronicle_zstd_sources})
    target_include_directories(chronicle_zstd SYSTEM PUBLIC ${zstd_SOURCE_DIR}/lib)
    target_compile_definitions(chronicle_zstd PRIVATE ZSTD_DISABLE_ASM)
    set_target_properties(chronicle_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
endif()

chronicle_vendor(chronicle_stb_image_write_dir stb_image_write.h
    https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image_write.h
    cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05)
if(NOT TARGET chronicle_stb_image_write)
    add_library(chronicle_stb_image_write INTERFACE)
    target_include_directories(chronicle_stb_image_write SYSTEM INTERFACE ${chronicle_stb_image_write_dir})
endif()

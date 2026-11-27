# Только кодеки для EXPORT SONG. Динамические библиотеки FFmpeg (LGPL).
include(ExternalProject)
find_program(SONG_MAKE NAMES gmake make REQUIRED)

get_filename_component(SONG_TOOLCHAIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
string(REGEX MATCH "[0-9]+$" SONG_API "${ANDROID_PLATFORM}")
if(NOT SONG_API)
  set(SONG_API "${CMAKE_SYSTEM_VERSION}")
endif()
set(SONG_CC "${SONG_TOOLCHAIN}/aarch64-linux-android${SONG_API}-clang")
set(SONG_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/song-codecs")
file(MAKE_DIRECTORY "${SONG_PREFIX}/include")

ExternalProject_Add(song_codecs
  URL https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz
  URL_HASH SHA256=089bc60fb59d6aecc5d994ff530fd0dcb3ee39aa55867849a2bbc4e555f9c304
  INSTALL_DIR "${SONG_PREFIX}"
  CONFIGURE_COMMAND <SOURCE_DIR>/configure
    --prefix=<INSTALL_DIR> --target-os=android --arch=aarch64 --enable-cross-compile
    --cc=${SONG_CC} --ar=${SONG_TOOLCHAIN}/llvm-ar --ranlib=${SONG_TOOLCHAIN}/llvm-ranlib
    --nm=${SONG_TOOLCHAIN}/llvm-nm --strip=${SONG_TOOLCHAIN}/llvm-strip
    --extra-ldflags=-Wl,-z,max-page-size=16384
    --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-avdevice --disable-avfilter --disable-swscale --disable-asm --disable-debug
    --enable-shared --disable-static --enable-avcodec --enable-avformat --enable-swresample
    --enable-encoder=aac,alac,pcm_s16le,pcm_s24le,pcm_f32le --enable-muxer=wav,ipod --enable-protocol=file
  BUILD_COMMAND ${SONG_MAKE} -j4
  INSTALL_COMMAND ${SONG_MAKE} install
  BUILD_BYPRODUCTS
    "${SONG_PREFIX}/lib/libavformat.so" "${SONG_PREFIX}/lib/libavcodec.so"
    "${SONG_PREFIX}/lib/libswresample.so" "${SONG_PREFIX}/lib/libavutil.so"
)

foreach(lib IN ITEMS avformat avcodec swresample avutil)
  add_library(song_${lib} SHARED IMPORTED)
  set_target_properties(song_${lib} PROPERTIES
    IMPORTED_LOCATION "${SONG_PREFIX}/lib/lib${lib}.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SONG_PREFIX}/include")
  add_dependencies(song_${lib} song_codecs)
endforeach()

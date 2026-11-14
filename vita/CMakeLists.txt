cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation before configuring")
  endif()
endif()

project(MuseVita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
find_package(CURL REQUIRED)

set(VITA_TITLEID "MSVT00001")
set(VITA_VERSION "00.10")
set(VITA_APP_NAME "MuseVita")

add_executable(musevita
  src/main.c
  src/chat_state.c
  src/muse_session.c
  src/vita_media.c
  src/network_state.c
  src/pairing_transfer.c
  src/qrcodegen.c
  ../protocol/control_framing.c
  ../protocol/json_extract.c
  ../protocol/link_wire.c
  ../protocol/noise_xx.c
)
target_include_directories(musevita PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../protocol)
# Keep the native executable below Vita's fixed ELF segment boundary. The
# transport is latency-bound on network I/O, so size optimization is a better
# fit than spending code space on a small speed gain.
target_compile_options(musevita PRIVATE -Wall -Wextra -Os)
target_link_libraries(musevita
  vita2d
  SceAppMgr_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceNet_stub
  SceNetCtl_stub
  ScePgf_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceAudioIn_stub
  SceCamera_stub
  SceIme_stub
  SceJpegEnc_stub
  SceKernelThreadMgr_stub
  ${CURL_LIBRARIES}
  zstd
  z
  mbedtls
  mbedcrypto
  mbedx509
  pthread
  m
)

vita_create_self(musevita.self musevita)
vita_create_vpk(MuseVita.vpk ${VITA_TITLEID} musevita.self
  VERSION ${VITA_VERSION}
  NAME "${VITA_APP_NAME}"
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png" sce_sys/icon0.png
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/pairing.html" sce_sys/pairing.html
)

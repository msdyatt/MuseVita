cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation before configuring")
  endif()
endif()

project(muse_vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_TITLEID "MSVT00001")
set(VITA_VERSION "00.10")
set(VITA_APP_NAME "Muse Vita (Prototype)")

add_executable(muse_vita src/main.c)
target_compile_options(muse_vita PRIVATE -Wall -Wextra -O2)
target_link_libraries(muse_vita
  vita2d
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceKernelThreadMgr_stub
  m
)

vita_create_self(muse_vita.self muse_vita)
vita_create_vpk(muse_vita.vpk ${VITA_TITLEID} muse_vita.self
  VERSION ${VITA_VERSION}
  NAME "${VITA_APP_NAME}"
)

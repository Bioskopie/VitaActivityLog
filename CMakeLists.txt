cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaActivityLog C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "3DS Activity Log")
set(VITA_TITLEID  "ACTLOG3DS")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -fno-builtin -Wno-format-truncation")

# 1. Build the 3DS Activity Log VPK Application
add_executable(VitaActivityLog main.c)

target_link_libraries(VitaActivityLog
  vita2d
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceIofilemgr_stub
  SceRtc_stub
  png
  jpeg
  z
  m
  c
)

vita_create_self(eboot.bin VitaActivityLog UNSAFE)
vita_create_vpk(VitaActivityLog.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)

# Reset VitaSDK macro flags before building the plugin
set(VITA_ELF_CREATE_FLAGS "")
set(VITA_MAKE_FSELF_FLAGS "")

# 2. Generate exports.yml and build the Background Plugin (activity_log.suprx)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.yml"
"ActivityLogPlugin:\n"
"  attributes: 0\n"
"  version:\n"
"    major: 1\n"
"    minor: 0\n"
"  main:\n"
"    start: module_start\n"
"    stop: module_stop\n"
)

add_executable(activity_log_elf plugin.c)
set_target_properties(activity_log_elf PROPERTIES LINK_FLAGS "-Wl,-q -nostdlib")

target_link_libraries(activity_log_elf
  SceLibc_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
  SceAppMgr_stub
  SceIofilemgr_stub
  SceRtc_stub
  gcc
)

vita_create_self(activity_log.suprx activity_log_elf CONFIG ${CMAKE_CURRENT_BINARY_DIR}/exports.yml UNSAFE)

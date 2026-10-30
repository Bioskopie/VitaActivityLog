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

set(VPK_EXTRA_ARGS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png")
  list(APPEND VPK_EXTRA_ARGS FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" sce_sys/icon0.png)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/startup.png")
  list(APPEND VPK_EXTRA_ARGS FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/startup.png" sce_sys/livearea/contents/startup.png)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png")
  list(APPEND VPK_EXTRA_ARGS FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png")
  list(APPEND VPK_EXTRA_ARGS FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml")
  list(APPEND VPK_EXTRA_ARGS FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml)
endif()

vita_create_vpk(VitaActivityLog.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${VPK_EXTRA_ARGS}
)

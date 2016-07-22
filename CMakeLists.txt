cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

PROJECT(vdpau_rockchip C)
INCLUDE(GNUInstallDirs)

pkg_search_module(LIBVDPAU vdpau)
pkg_search_module(LIBDRM libdrm)
pkg_search_module(LIBGLESV2 glesv2)
pkg_search_module(LIBX11 x11)
ADD_DEFINITIONS(-Os -Wall --std=gnu99 -g3 -Wmissing-declarations)

ADD_SUBDIRECTORY (librkdec)

ADD_LIBRARY(vdpau_rockchip SHARED
decoder.c device.c gles.c h264_decoder.c handles.c
presentation_queue.c rgba.c surface_bitmap.c surface_output.c
surface_video.c v4l2.c video_mixer.c
)

TARGET_LINK_LIBRARIES(vdpau_rockchip
${LIBVDPAU_LIBRARIES}
${LIBDRM_LIBRARIES}
${LIBGLESV2_LIBRARIES}
${LIBX11_LIBRARIES}
rkdec
m
)

TARGET_INCLUDE_DIRECTORIES(vdpau_rockchip PUBLIC 
"${LIBVDPAU_INCLUDE_DIRS}"
"${LIBDRM_INCLUDE_DIRS}"
"${LIBGLESV2_INCLUDE_DIRS}"
"${LIBX11_INCLUDE_DIRS}"
"${CMAKE_CURRENT_SOURCE_DIR}/include"
"${CMAKE_CURRENT_SOURCE_DIR}/librkdec/include"
)

TARGET_COMPILE_OPTIONS(vdpau_rockchip PUBLIC 
${LIBVDPAU_CFLAGS} ${LIBDRM_CFLAGS} ${LIBGLESV2_CFLAGS} ${LIBX11_CFLAGS}
)

INSTALL(TARGETS vdpau_rockchip LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

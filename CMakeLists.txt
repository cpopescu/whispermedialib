# Copyright (c) 2009, Whispersoft s.r.l.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
#
# Main cmake configuration file - we set a bunch of things here ..
#
# To build the project(s), run in this directory:
#    cmake .
#    make
#
# To build in Debug mode:
#    cmake . -DCMAKE_BUILD_TYPE=Debug
#    make
#
# Specify the install dir with:
#    cmake -DINSTALL_PREFIX=<prefix_dir> ...
#
# You can continue with testing:
#    make test
# And installation of the binaries:
#    make install
#
######################################################################

project (whispermedialib)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_MICRO "1")
set(VERSION_PATCH "0")

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Media encoding library for Linux.")

cmake_minimum_required(VERSION 2.4)

IF(COMMAND cmake_policy AND ${CMAKE_MAJOR_VERSION} EQUAL 2 AND ${CMAKE_MINOR_VERSION} GREATER 4)
  CMAKE_POLICY(SET CMP0002 NEW)
  CMAKE_POLICY(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy AND ${CMAKE_MAJOR_VERSION} EQUAL 2 AND ${CMAKE_MINOR_VERSION} GREATER 4)

######################################################################

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_SOURCE_DIR}/../cmake ${CMAKE_MODULE_PATH})

include(../cmake/common.cmake)

find_package(WhisperLib REQUIRED)
find_package(WhisperStreamLib REQUIRED)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../)
include_directories(${whisperlib_INCLUDE_DIR})
include_directories(${whisperstreamlib_INCLUDE_DIR})

add_subdirectory(third-party)

######################################################################

include(FindPkgConfig)

if ($ENV{PKG_CONFIG_PATH})
  set(ENV{PKG_CONFIG_PATH}
    "${CMAKE_CURRENT_BINARY_DIR}/third-party/gstreamer/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
else ($ENV{PKG_CONFIG_PATH})
  set(ENV{PKG_CONFIG_PATH}
    "${CMAKE_CURRENT_SOURCE_DIR}/third-party/gstreamer/lib/pkgconfig")
endif ($ENV{PKG_CONFIG_PATH})

pkg_check_modules(GST REQUIRED gstreamer-0.10)

include_directories(${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

pkg_check_modules(GST_DP gstreamer-dataprotocol-0.10)
if (NOT GST_DP_FOUND)
  message (FATAL_ERROR "It seems that the required GStreamer libraries "
    "are not available.")
endif (NOT GST_DP_FOUND)

include_directories(${GST_DP_INCLUDE_DIRS})
link_directories(${GST_DP_LIBRARY_DIRS})

######################################################################

if ( NOT RPC_PARSER_TARGET )
  set(RPC_PARSER_TARGET "")
endif ( NOT RPC_PARSER_TARGET )

link_libraries(${COMMON_LINK_LIBRARIES})

pkg_check_modules(Fftw3 fftw3)
if (NOT Fftw3_FOUND)
  find_package(Fftw3 REQUIRED)
endif (NOT Fftw3_FOUND)

message(STATUS "FFTW3 library: " ${Fftw3_LIBRARIES})
################################################################################
#
# whisper_medialib
#
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/media/auto)
set(RPC_INPUT_FILES  ${CMAKE_CURRENT_SOURCE_DIR}/media/controller.rpc)
set(RPC_OUTPUT_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/types.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/invokers.cc)
add_custom_command(
  OUTPUT ${RPC_OUTPUT_FILES}
  COMMAND ${RPC_PARSER_EXE} ${RPC_INPUT_FILES}
  --cc='fServerTypes="${CMAKE_CURRENT_SOURCE_DIR}/media/auto/types", fServerInvokers="${CMAKE_CURRENT_SOURCE_DIR}/media/auto/invokers", includePathCut="${CMAKE_CURRENT_SOURCE_DIR}/", includePathPaste="whispermedialib/"'
  --php='fTypes="${CMAKE_CURRENT_SOURCE_DIR}/media/auto/types.php", fWrappers="${CMAKE_CURRENT_SOURCE_DIR}/media/auto/invokers.php"'
  DEPENDS ${RPC_INPUT_FILES} ${RPC_PARSER_TARGET}
  )

##########

add_library(
  whisper_medialib SHARED
  media/adapter_audio.cc
  media/adapter_video.cc
  media/component.cc
  media/controller.cc
  media/controller_v4l2.cc
  media/controller_v4l.cc
  media/encoder_audio.cc
  media/encoder_video.cc
  media/pipeline.cc
  media/pipeline_runner.cc
  media/sink_file.cc
  media/sink_multifd.cc
  media/sink_tcp.cc
  media/source_audio.cc
  media/source_fd.cc
  media/source_file.cc
  media/source_tcp.cc
  media/source_video.cc
  media/streaming.cc
  media/stream_processor.cc
  media/stream_provider.cc
  media/gst/gstaacp.cc
  ${RPC_OUTPUT_FILES})

add_dependencies(
  whisper_medialib
  whisper_imported_aacplusenc
  gstreamer
  )

target_link_libraries(
  whisper_medialib
  whisper_imported_aacplusenc
  ${whisperlib_LIBRARY}
  ${GST_LIBRARIES}
  ${GST_DP_LIBRARIES}
  ${Fftw3_LIBRARIES}
  fftw3f
  gstinterfaces-0.10)

add_subdirectory (media/js)

######################################################################
#
# INSTALL
#

install (TARGETS whisper_medialib
         DESTINATION lib)

#install(DIRECTORY DESTINATION bin)
#install(DIRECTORY DESTINATION lib)
#install(DIRECTORY DESTINATION include)
#install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX_VAR}/www/js")

install(FILES
  media/adapter_audio.h
  media/adapter_video.h
  media/component.h
  media/controller.h
  media/controller_v4l.h
  media/controller_v4l2.h
  media/encoder_audio.h
  media/encoder_video.h
  media/gst/gstaacp.h
  media/pipeline.h
  media/pipeline_runner.h
  media/sink_file.h
  media/sink_multifd.h
  media/sink_tcp.h
  media/source_audio.h
  media/source_fd.h
  media/source_file.h
  media/source_tcp.h
  media/source_video.h
  media/stream_processor.h
  media/stream_provider.h
  media/streaming.h
  DESTINATION include/whispermedialib/media)

install(FILES
  media/auto/types.h
  media/auto/invokers.h
  DESTINATION include/whispermedialib/media/auto)

FILE(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX_SCRIPTS}/php/rpc/auto/whispermedialib/media)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/invokers.php
  ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/types.php
  DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS}/php/rpc/auto/whispermedialib/media)

if (WITH_ADMIN)
  FILE(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX_ADMIN}/rpc/auto/whispermedialib/media)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/invokers.php
    ${CMAKE_CURRENT_SOURCE_DIR}/media/auto/types.php
    DESTINATION ${CMAKE_INSTALL_PREFIX_ADMIN}/rpc/auto/whispermedialib/media)
endif (WITH_ADMIN)

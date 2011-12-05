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

project (whispermedialib)

include(FindPkgConfig)

find_package(Bison REQUIRED)
find_package(Flex REQUIRED)
find_package(GetText REQUIRED)

#############################
#
#  find_pakage already defined by cmake
#
find_package(OpenSSL REQUIRED)
find_package(OpenGL REQUIRED)

find_package(LibXml2 REQUIRED)
find_package(BZip2 REQUIRED)

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)

pkg_check_modules(Mpeg2 libmpeg2)
if (NOT Mpeg2_FOUND)
  find_package(Mpeg2 REQUIRED)
endif (NOT Mpeg2_FOUND)

pkg_check_modules(Raw1394 libraw1394)
if (NOT Raw1394_FOUND)
  find_package(Raw1394 REQUIRED)
endif (NOT Raw1394_FOUND)

pkg_check_modules(Avc1394 libavc1394)
if (NOT Avc1394_FOUND)
  find_package(Avc1394 REQUIRED)
endif (NOT Avc1394_FOUND)

pkg_check_modules(Id3Tag id3tag)
if (NOT Id3Tag_FOUND)
  find_package(Id3Tag REQUIRED)
endif (NOT Id3Tag_FOUND)

pkg_check_modules(Iec61883 libiec61883)
if (NOT Iec61883_FOUND)
  find_package(Iec61883 REQUIRED)
endif (NOT Iec61883_FOUND)

pkg_check_modules(Dv libdv)
if (NOT Dv_FOUND)
  find_package(Dv REQUIRED)
endif (NOT Dv_FOUND)

pkg_check_modules(X264 x264)
if (NOT X264_FOUND)
  find_package(X264 REQUIRED)
endif (NOT X264_FOUND)

pkg_check_modules(Neon neon)
if (NOT Neon_FOUND)
  find_package(Neon REQUIRED)
endif (NOT Neon_FOUND)

pkg_check_modules(Fftw3 fftw3)
if (NOT Fftw3_FOUND)
  find_package(Fftw3 REQUIRED)
endif (NOT Fftw3_FOUND)

pkg_check_modules(LibOil_0_3 liboil-0.3)
if (NOT LibOil_0_3_FOUND)
  find_package(LibOil_0_3 REQUIRED)
endif (NOT LibOil_0_3_FOUND)

find_package(Lame REQUIRED)
find_package(Faac REQUIRED)
find_package(Faad REQUIRED)
find_package(Pango REQUIRED)
find_package(Esd REQUIRED)
find_package(A52 REQUIRED)
find_package(Asound REQUIRED)

######################################################################

# Not required stuff

find_package(GTK)

find_package(X11)

pkg_check_modules(Ogg ogg)
if (NOT Ogg_FOUND)
  find_package(Ogg)
endif (NOT Ogg_FOUND)

pkg_check_modules(GnomeVFS gnome-vfs-2.0)
if (NOT GnomeVFS_FOUND)
  find_package(GnomeVFS)
endif (NOT GnomeVFS_FOUND)

pkg_check_modules(Theora theora)
if (NOT Theora_FOUND)
  find_package(Theora)
endif (NOT Theora_FOUND)

pkg_check_modules(Vorbis theora)
if (NOT Vorbis_FOUND)
  find_package(Vorbis REQUIRED)
endif (NOT Vorbis_FOUND)

pkg_check_modules(FLAC flac)
 if (NOT FLAC_FOUND)
   find_package(FLAC)
endif (NOT FLAC_FOUND)

pkg_check_modules(Speex speex)
if (NOT Speex_FOUND)
  find_package(Speex)
endif (NOT Speex_FOUND)

pkg_check_modules(DTS dts)
if (NOT DTS_FOUND)
  find_package(DTS)
endif (NOT DTS_FOUND)

find_package(GSM)
find_package(Audio)

#  Build gstreamer
execute_process(COMMAND ./gstreamer-make new "${CMAKE_CURRENT_BINARY_DIR}/third-party"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE GST_BUILD_RESULT)

if (GST_BUILD_RESULT)
  message (FATAL_ERROR "Cannot build the required GStreamer libraries.")
endif (GST_BUILD_RESULT)

include(FindPkgConfig)

if ($ENV{PKG_CONFIG_PATH})
  set(ENV{PKG_CONFIG_PATH}
    "${CMAKE_CURRENT_BINARY_DIR}/third-party/gstreamer/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
else ($ENV{PKG_CONFIG_PATH})
  set(ENV{PKG_CONFIG_PATH}
    "${CMAKE_CURRENT_BINARY_DIR}/third-party/gstreamer/lib/pkgconfig")
endif ($ENV{PKG_CONFIG_PATH})

pkg_check_modules(GST gstreamer-0.10)
if (NOT GST_FOUND)
  message (FATAL_ERROR "It seems that the required GStreamer libraries "
    "are not available.")
endif (NOT GST_FOUND)

include_directories(${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

pkg_check_modules(GST_DP gstreamer-dataprotocol-0.10)
if (NOT GST_DP_FOUND)
  message (FATAL_ERROR "It seems that the required GStreamer data "
    "protocol libraries are not available.")
ENDIF (NOT GST_DP_FOUND)

include_directories(${GST_DP_INCLUDE_DIRS})
link_directories(${GST_DP_LIBRARY_DIRS})

install(
  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/third-party/gstreamer"
  DESTINATION lib
  USE_SOURCE_PERMISSIONS)

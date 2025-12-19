# Copyright 2013, 2019 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

include(CMakeFindDependencyMacro)

# Allows us to use all .cmake files in this directory
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}")

find_dependency(spdlog)
find_dependency(MPLIB)

set(BOOST_REQUIRED_COMPONENTS date_time program_options system regex thread)

if(NOT ENABLE_TESTING)
    set(ENABLE_TESTING
        OFF
        CACHE BOOL "Enable testing support")
endif()

if(ENABLE_TESTING)
    list(APPEND BOOST_REQUIRED_COMPONENTS unit_test_framework)
endif(ENABLE_TESTING)

find_dependency(
    Boost "1.84.0"
    COMPONENTS ${BOOST_REQUIRED_COMPONENTS})
find_dependency(Volk)
set(ENABLE_PYTHON
    OFF
    CACHE BOOL "Enable Python & pybind11 Bindings")
if(${ENABLE_PYTHON})
    set(PYTHON_EXECUTABLE /usr/bin/python3)
    include(GrPython)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-pmtConfig.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-runtimeConfig.cmake")
cmake_policy(SET CMP0012 NEW)
cmake_policy(SET CMP0057 NEW)

# Propagate the enabledness of GRC:
# Since GRC doesn't contain proper CMake targets (yet?), we need to do this
# manually
set(ENABLE_GRC
    
    CACHE BOOL "Enable GRC features (export block YAML)")
if()
    set(GRC_BLOCKS_DIR "share/gnuradio/grc/blocks")
endif()
# Same for a few "special" features.
set(GR_PERFORMANCE_COUNTERS True)
set(GR_CTRLPORT )
set(GR_RPCSERVER_ENABLED )
set(ENABLE_CTRLPORT_THRIFT OFF)
set(GR_RPCSERVER_THRIFT )

# Propagate global options
set(GR_RUNTIME_DIR "bin")
set(GR_LIBRARY_DIR "lib")
set(GR_DATA_DIR "share")
set(GR_PKG_DATA_DIR "share/gnuradio")
set(GR_DOC_DIR "share/doc")
set(GR_PKG_DOC_DIR "share/doc/gnuradio-3.10.11.0")
set(GR_LIBEXEC_DIR "libexec")
set(GR_PKG_LIBEXEC_DIR "libexec/gnuradio")
set(GR_THEMES_DIR "share/gnuradio/themes")
set(GR_CONF_DIR "etc")
set(SYSCONFDIR "/opt/gnuradio/etc")
set(GR_PREFSDIR "/opt/gnuradio/etc/gnuradio/conf.d")

# We check the requested components in the order given by the list below –
# ordering matters; we have module interdependencies.
set(GR_COMPONENTS
    blocks
    fec
    fft
    filter
    analog
    digital
    dtv
    audio
    channels
    pdu
    iio
    qtgui
    trellis
    uhd
    video-sdl
    vocoder
    wavelet
    zeromq
    network
    soapy)

foreach(target ${GR_COMPONENTS})
    set(GR_COMPONENT_INCLUDE_FILE
        "${CMAKE_CURRENT_LIST_DIR}/gnuradio-${target}Config.cmake")
    if(${target} IN_LIST Gnuradio_FIND_COMPONENTS
       AND (EXISTS ${GR_COMPONENT_INCLUDE_FILE} OR "${Gnuradio_FIND_REQUIRED_${target}}"))
        include(${GR_COMPONENT_INCLUDE_FILE})
    endif()
endforeach(target)

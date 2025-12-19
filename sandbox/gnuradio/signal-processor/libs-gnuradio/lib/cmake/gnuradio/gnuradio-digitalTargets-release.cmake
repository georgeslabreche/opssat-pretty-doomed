#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gnuradio::gnuradio-digital" for configuration "Release"
set_property(TARGET gnuradio::gnuradio-digital APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(gnuradio::gnuradio-digital PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libgnuradio-digital.so.3.10.11.0"
  IMPORTED_SONAME_RELEASE "libgnuradio-digital.so.3.10.11"
  )

list(APPEND _cmake_import_check_targets gnuradio::gnuradio-digital )
list(APPEND _cmake_import_check_files_for_gnuradio::gnuradio-digital "${_IMPORT_PREFIX}/lib/libgnuradio-digital.so.3.10.11.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

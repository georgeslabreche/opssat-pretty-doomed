#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gnuradio::gnuradio-analog" for configuration "Release"
set_property(TARGET gnuradio::gnuradio-analog APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(gnuradio::gnuradio-analog PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "gnuradio::gnuradio-filter"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libgnuradio-analog.so.3.10.11.0"
  IMPORTED_SONAME_RELEASE "libgnuradio-analog.so.3.10.11"
  )

list(APPEND _cmake_import_check_targets gnuradio::gnuradio-analog )
list(APPEND _cmake_import_check_files_for_gnuradio::gnuradio-analog "${_IMPORT_PREFIX}/lib/libgnuradio-analog.so.3.10.11.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

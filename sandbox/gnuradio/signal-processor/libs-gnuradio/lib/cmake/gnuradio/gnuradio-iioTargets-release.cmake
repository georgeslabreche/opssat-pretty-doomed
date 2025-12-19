#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gnuradio::gnuradio-iio" for configuration "Release"
set_property(TARGET gnuradio::gnuradio-iio APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(gnuradio::gnuradio-iio PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "gnuradio::gnuradio-blocks;Volk::volk"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libgnuradio-iio.so.3.10.11.0"
  IMPORTED_SONAME_RELEASE "libgnuradio-iio.so.3.10.11"
  )

list(APPEND _cmake_import_check_targets gnuradio::gnuradio-iio )
list(APPEND _cmake_import_check_files_for_gnuradio::gnuradio-iio "${_IMPORT_PREFIX}/lib/libgnuradio-iio.so.3.10.11.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

#----------------------------------------------------------------
# Generated CMake target import file for configuration "".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "armadillo" for configuration ""
set_property(TARGET armadillo APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(armadillo PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib64/libarmadillo.so.7.960.1"
  IMPORTED_SONAME_NOCONFIG "libarmadillo.so.7"
  )

list(APPEND _IMPORT_CHECK_TARGETS armadillo )
list(APPEND _IMPORT_CHECK_FILES_FOR_armadillo "${_IMPORT_PREFIX}/lib64/libarmadillo.so.7.960.1" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

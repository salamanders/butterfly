#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tensorflow-lite::tensorflow-lite" for configuration "Release"
set_property(TARGET tensorflow-lite::tensorflow-lite APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tensorflow-lite::tensorflow-lite PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libtensorflow-lite.so.2.20.0"
  IMPORTED_SONAME_RELEASE "libtensorflow-lite.so.2.20.0"
  )

list(APPEND _cmake_import_check_targets tensorflow-lite::tensorflow-lite )
list(APPEND _cmake_import_check_files_for_tensorflow-lite::tensorflow-lite "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libtensorflow-lite.so.2.20.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

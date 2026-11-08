#----------------------------------------------------------------
# Generated CMake target import file for configuration "None".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "apriltag::apriltag" for configuration "None"
set_property(TARGET apriltag::apriltag APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(apriltag::apriltag PROPERTIES
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libapriltag.so.3.4.2"
  IMPORTED_SONAME_NONE "libapriltag.so.3"
  )

list(APPEND _cmake_import_check_targets apriltag::apriltag )
list(APPEND _cmake_import_check_files_for_apriltag::apriltag "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libapriltag.so.3.4.2" )

# Import target "apriltag::apriltag-utils" for configuration "None"
set_property(TARGET apriltag::apriltag-utils APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(apriltag::apriltag-utils PROPERTIES
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libapriltag-utils.so.3.4.2"
  IMPORTED_SONAME_NONE "libapriltag-utils.so.3"
  )

list(APPEND _cmake_import_check_targets apriltag::apriltag-utils )
list(APPEND _cmake_import_check_files_for_apriltag::apriltag-utils "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libapriltag-utils.so.3.4.2" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

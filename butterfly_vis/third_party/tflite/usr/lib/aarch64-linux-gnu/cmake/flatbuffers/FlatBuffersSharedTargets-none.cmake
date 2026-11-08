#----------------------------------------------------------------
# Generated CMake target import file for configuration "None".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "flatbuffers::flatbuffers_shared" for configuration "None"
set_property(TARGET flatbuffers::flatbuffers_shared APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(flatbuffers::flatbuffers_shared PROPERTIES
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libflatbuffers.so.24.3.25"
  IMPORTED_SONAME_NONE "libflatbuffers.so.24.3.25"
  )

list(APPEND _cmake_import_check_targets flatbuffers::flatbuffers_shared )
list(APPEND _cmake_import_check_files_for_flatbuffers::flatbuffers_shared "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libflatbuffers.so.24.3.25" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

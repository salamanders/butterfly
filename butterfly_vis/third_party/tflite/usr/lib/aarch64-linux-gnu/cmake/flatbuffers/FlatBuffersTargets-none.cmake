#----------------------------------------------------------------
# Generated CMake target import file for configuration "None".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "flatbuffers::flatbuffers" for configuration "None"
set_property(TARGET flatbuffers::flatbuffers APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(flatbuffers::flatbuffers PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NONE "CXX"
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libflatbuffers.a"
  )

list(APPEND _cmake_import_check_targets flatbuffers::flatbuffers )
list(APPEND _cmake_import_check_files_for_flatbuffers::flatbuffers "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libflatbuffers.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

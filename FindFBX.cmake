#
# Helper function for finding the FBX SDK.
# Cribbed & tweaked from https://github.com/floooh/fbxc/
#
# params: FBXSDK_VERSION
#         FBXSDK_SDKS
#
# sets:   FBXSDK_FOUND
#         FBXSDK_DIR
#         FBXSDK_LIBRARY
#         FBXSDK_LIBRARY_DEBUG
#         FBXSDK_INCLUDE_DIR
#

# semi-hack to detect architecture
if( CMAKE_SIZEOF_VOID_P MATCHES 8 )
  # void ptr = 8 byte --> x86_64
  set(ARCH_32 OFF)
else()
  # void ptr != 8 byte --> x86
  set(ARCH_32 OFF)
endif()

set(FBXSDK_VERSION "2018.1.1" CACHE STRING "Precise version string of FBX SDK to use.")

set(_fbxsdk_vstudio_version "vs2015")

message("Looking for FBX SDK version: ${FBXSDK_VERSION}")

set(FBXSDK_WINDOWS_ROOT "./ThirdParty/FBXSDK/${FBXSDK_VERSION}")

set(_fbxsdk_root "${FBXSDK_WINDOWS_ROOT}")
set(_fbxsdk_libdir_debug "lib/${_fbxsdk_vstudio_version}/x64/debug")
set(_fbxsdk_libdir_release "lib/${_fbxsdk_vstudio_version}/x64/release")
set(_fbxsdk_libname_debug "libfbxsdk-md.lib")
set(_fbxsdk_libname_release "libfbxsdk-md.lib")

# should point the the FBX SDK installation dir
set(FBXSDK_ROOT "${_fbxsdk_root}")
message("FBXSDK_ROOT: ${FBXSDK_ROOT}")

# find header dir and libs
find_path(FBXSDK_INCLUDE_DIR "fbxsdk.h"
  NO_CMAKE_FIND_ROOT_PATH
  PATHS ${FBXSDK_ROOT}
  PATH_SUFFIXES "include")
message("FBXSDK_INCLUDE_DIR: ${FBXSDK_INCLUDE_DIR}")

find_library(FBXSDK_LIBRARY ${_fbxsdk_libname_release}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_release}")
message("FBXSDK_LIBRARY: ${FBXSDK_LIBRARY}")

find_library(FBXSDK_LIBRARY_DEBUG ${_fbxsdk_libname_debug}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_debug}")
message("FBXSDK_LIBRARY_DEBUG: ${FBXSDK_LIBRARY_DEBUG}")

if (FBXSDK_INCLUDE_DIR AND FBXSDK_LIBRARY AND FBXSDK_LIBRARY_DEBUG)
  set(FBXSDK_FOUND YES)
else()
  set(FBXSDK_FOUND NO)
endif()

cmake_minimum_required(VERSION 3.16)
project(rclone-browser VERSION 1.0 LANGUAGES CXX)

if(WIN32)
  # link automatically to qtmain.lib on Windows
  cmake_policy(SET CMP0020 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This project supports only 64-bit Windows builds.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

if(APPLE)
  # macOS extras if needed
  find_library(COCOA_LIB Cocoa REQUIRED)
  find_library(IOKKIT_LIB IOKit REQUIRED)
endif()

if(WIN32)
  set_property(GLOBAL PROPERTY USE_FOLDERS OFF)

  add_definitions("-D_UNICODE -DUNICODE -D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /GF /Gy /GS- /GR /GL")

  set(CMAKE_EXE_LINKER_FLAGS "/INCREMENTAL:NO")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:ICF /OPT:REF")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RCLONE_BROWSER_VERSION)
  add_definitions(-DRCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
else()
  message(WARNING "VERSION file not found. Defaulting RCLONE_BROWSER_VERSION to 3.0.0.")
  add_definitions(-DRCLONE_BROWSER_VERSION="3.0.0")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build")

add_subdirectory(src)

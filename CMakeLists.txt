cmake_minimum_required(VERSION 3.16)

if(WIN32)
  # Match Qt's prebuilt MSVC libraries (/MD) and avoid LNK4098 CRT conflicts.
  # Must be set before project() for the policy to take effect.
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  # link automatically to qtmain.lib on Windows
  cmake_policy(SET CMP0020 NEW)
endif()

project(rclone-browser VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This project supports only 64-bit Windows builds.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# Multimedia is only used for the optional finished-transfer notification
# sound, and is not packaged by every distribution. Treat it as optional so
# the app still builds on Qt kits that do not ship it.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)

if(APPLE)
  # macOS extras if needed
  find_library(COCOA_LIB Cocoa REQUIRED)
  find_library(IOKKIT_LIB IOKit REQUIRED)
  find_library(USER_NOTIFICATIONS_LIB UserNotifications REQUIRED)
  find_library(UNIFORM_TYPE_IDENTIFIERS_LIB UniformTypeIdentifiers REQUIRED)
endif()

if(WIN32)
  set_property(GLOBAL PROPERTY USE_FOLDERS OFF)

  add_definitions("-D_UNICODE -DUNICODE -D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE")

  # Without this MSVC reads sources in the system code page, which on a
  # non-Latin locale mangles any non-ASCII byte -- including the ones in the
  # comments in this tree. The symptom is a translation unit that compiles to
  # something subtly wrong rather than an error.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /utf-8")
  # Was "${CMAKE_CXX_FLAGS} ... /GS-": it expanded the wrong variable (so the
  # base flags were duplicated into the Release flags) and it disabled stack
  # buffer overrun detection. /GS is on, plus Control Flow Guard.
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GF /Gy /GS /GR /GL /guard:cf")

  set(CMAKE_EXE_LINKER_FLAGS "/INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
  # /guard:cf must be passed to the linker too, or the compiler metadata is
  # dropped. /CETCOMPAT opts into Intel CET shadow stacks.
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:ICF /OPT:REF /guard:cf /CETCOMPAT")
else()
  # OpenSSF-recommended hardening for GCC/Clang builds (Linux, macOS, BSD).
  add_compile_options(-fstack-protector-strong)
  if(NOT APPLE)
    add_compile_options(-fcf-protection=full)
    add_link_options(-Wl,-z,relro,-z,now)
  endif()
  add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>)
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

option(RB_BUILD_TESTS "Build the rbcore unit tests" ON)
if(RB_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    enable_testing()
    add_subdirectory(tests)
  else()
    message(STATUS "Qt6 Test not found - skipping unit tests")
  endif()
endif()

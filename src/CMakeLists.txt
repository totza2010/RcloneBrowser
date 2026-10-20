cmake_minimum_required(VERSION 3.16)
project(rclone-browser LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(Qt6_VERSION VERSION_LESS 6.0.0)
    message(FATAL_ERROR "Minimum supported Qt6 version is 6.0.0")
endif()

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This project supports only 64-bit Windows builds.")
endif()

# ---- Qt6 Components ----
if(RB_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql)
else()
  find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)
endif()
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)

if(Qt6Multimedia_FOUND)
    set(RB_MULTIMEDIA_LIB Qt6::Multimedia)
    message(STATUS "Qt6 Multimedia found - transfer notification sound enabled")
else()
    set(RB_MULTIMEDIA_LIB "")
    message(STATUS "Qt6 Multimedia not found - building without notification sound")
endif()

if(APPLE)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(IOKKIT_LIB IOKit REQUIRED)
    # Required by mac_os_notifications.mm on macOS 11+; linking failed silently
    # without these.
    find_library(USER_NOTIFICATIONS_LIB UserNotifications REQUIRED)
    find_library(UNIFORM_TYPE_IDENTIFIERS_LIB UniformTypeIdentifiers REQUIRED)
endif()

# ---- rbcore: everything that must build without QtWidgets ----
# Kept as a separate target so the boundary is enforced by the linker rather
# than by convention, and so the tests can exercise it without a GUI.
# See docs/ARCHITECTURE.md.
set(CORE_SOURCE
  utils.cpp
  app_settings.cpp
  debug_log.cpp
  job_options.cpp
  list_of_job_options.cpp
  rclone_capabilities.cpp
  rclone_flags.cpp
  rclone_config.cpp
  task_runner.cpp
  running_job.cpp
  job_registry.cpp
  job_queue.cpp
  app_core.cpp
  http_message.cpp
  api_server.cpp
  api_routes.cpp
  api_auth.cpp
  shutdown_signal.cpp
  remote_registry.cpp
  remote_ops.cpp
  local_files.cpp
  task_json.cpp
  extra_flags.cpp
  rclone_version.cpp
  task_builder.cpp
  schedule.cpp
  schedule_json.cpp
  scheduler_store.cpp
  script_runner.cpp
  job_log.cpp
  database.cpp
  config_store.cpp
  run_history.cpp
  job_stats.cpp
  lsjson_parser.cpp
  directory_listing.cpp
  file_reader.cpp
  job_json.cpp
  backup_plan.cpp
  folder_watches.cpp
  program_settings.cpp
  webhooks.cpp
  rc_client.cpp
  qcron.cpp
  qcronfield.cpp
  qcronnode.cpp
)

set(CORE_HEADER
  utils.h
  app_settings.h
  debug_log.h
  job_options.h
  list_of_job_options.h
  rclone_capabilities.h
  rclone_flags.h
  rclone_config.h
  task_runner.h
  running_job.h
  job_registry.h
  job_queue.h
  app_core.h
  http_message.h
  api_server.h
  api_routes.h
  api_auth.h
  shutdown_signal.h
  remote_registry.h
  remote_ops.h
  local_files.h
  task_json.h
  extra_flags.h
  rclone_version.h
  task_builder.h
  schedule.h
  schedule_json.h
  scheduler_store.h
  script_runner.h
  job_log.h
  database.h
  config_store.h
  run_history.h
  job_stats.h
  lsjson_parser.h
  directory_listing.h
  file_reader.h
  job_json.h
  backup_plan.h
  folder_watches.h
  program_settings.h
  webhooks.h
  rc_client.h
  qcron.h
  qcronfield.h
  qcronnode.h
  global.h
  pch_core.h
)

# The Web UI travels inside the library that serves it, so a daemon has no
# files to go looking for. See docs/WEB-UI.md.
set(CORE_RESOURCE webui.qrc remote_icons.qrc)

# The page's languages: every webui/i18n/<language>.json, compiled in under
# :/webui/i18n/. Listed when CMake runs, so a new file needs CMake run again
# (CONFIGURE_DEPENDS does that on the next build).
file(GLOB WEB_I18N CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/webui/i18n/*.json")
set(WEB_I18N_QRC "${CMAKE_CURRENT_BINARY_DIR}/webui_i18n.qrc")
set(WEB_I18N_FILES "")
foreach(one ${WEB_I18N})
  get_filename_component(name "${one}" NAME)
  string(APPEND WEB_I18N_FILES "    <file alias=\"${name}\">${one}</file>\n")
endforeach()
file(WRITE "${WEB_I18N_QRC}.in"
     "<RCC>\n  <qresource prefix=\"/webui/i18n\">\n${WEB_I18N_FILES}  </qresource>\n</RCC>\n")
configure_file("${WEB_I18N_QRC}.in" "${WEB_I18N_QRC}" COPYONLY)
list(APPEND CORE_RESOURCE "${WEB_I18N_QRC}")

add_library(rbcore STATIC ${CORE_SOURCE} ${CORE_HEADER} ${CORE_RESOURCE})
# No Qt6::Widgets and no Qt6::Gui here. That omission is the contract.
# Qt6::Sql is not a GUI module, so the contract above still holds: it brings
# in the SQLite driver and nothing that needs a screen. See docs/PLAN.md 6.8.
target_link_libraries(rbcore PUBLIC Qt6::Core Qt6::Network Qt6::Sql)
target_include_directories(rbcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(rbcore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch_core.h")

# ---- The program with no window (RB_BUILD_GUI=OFF) ----
# The core and its own main(): the daemon, --run-task and --list-tasks, the
# API and the page. No QtWidgets, no QtGui, nothing to draw with -- which is
# what the web-only image is made from. Same name as the window's program, so
# whatever starts one starts the other.
if(NOT RB_BUILD_GUI)
  add_executable(rclone-browser headless.cpp headless.h daemon_main.cpp)
  target_link_libraries(rclone-browser PRIVATE rbcore)
  install(TARGETS rclone-browser RUNTIME DESTINATION bin)
  return()
endif()

set(UI
  main_window.ui
  remote_widget.ui
  transfer_dialog.ui
  mount_dialog.ui
  export_dialog.ui
  check_dialog.ui
  dedupe_dialog.ui
  progress_dialog.ui
  job_widget.ui
  mount_widget.ui
  stream_widget.ui
  preferences_dialog.ui
  scheduler_widget.ui
  delete_progress_dialog.ui
  remote_folder_dialog.ui
)

set(MOC
  main_window.h
  remote_widget.h
  transfer_dialog.h
  mount_dialog.h
  export_dialog.h
  check_dialog.h
  dedupe_dialog.h
  progress_dialog.h
  job_widget.h
  history_widget.h
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  item_model.h
  scheduler_widget.h
  hours_spinbox.h
  minutes_spinbox.h
  delete_progress_dialog.h
  file_dialog.h
  remote_folder_dialog.h
  script_editor_dialog.h
  remote_settings_dialog.h
  serve_dialog.h
  window_language.h
  file_view_dialog.h
  webhooks_dialog.h
  backup_dialog.h
  api_keys_dialog.h
)

set(OTHER
  pch.h
  job_options_item.h
  widget_settings.h
  completers.h
)

set(SOURCE
  pch.cpp
  headless.cpp
  main_window.cpp
  remote_widget.cpp
  transfer_dialog.cpp
  mount_dialog.cpp
  export_dialog.cpp
  check_dialog.cpp
  dedupe_dialog.cpp
  progress_dialog.cpp
  job_widget.cpp
  history_widget.cpp
  mount_widget.cpp
  stream_widget.cpp
  preferences_dialog.cpp
  completers.cpp
  icon_cache.cpp
  item_model.cpp
  widget_settings.cpp
  scheduler_widget.cpp
  hours_spinbox.cpp
  minutes_spinbox.cpp
  delete_progress_dialog.cpp
  file_dialog.cpp
  remote_folder_dialog.cpp
  script_editor_dialog.cpp
  remote_settings_dialog.cpp
  serve_dialog.cpp
  window_language.cpp
  file_view_dialog.cpp
  webhooks_dialog.cpp
  backup_dialog.cpp
  api_keys_dialog.cpp
)

if(APPLE)
  set(OTHER ${OTHER} osx_helper.h mac_os_notifications.h mac_os_power_saving.h)
  set(SOURCE ${SOURCE} osx_helper.mm mac_os_notifications.mm mac_os_power_saving.mm)
endif()

set(QRC resources.qrc)

add_definitions(-DRCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")

if(Qt6Multimedia_FOUND)
  add_definitions(-DRB_HAVE_MULTIMEDIA)
endif()

# ---- The window, as a library ----
# Everything of the window but main(): the program links it, and so do the
# tests that click through the window (tests/test_window_*.cpp) -- the same
# code, not a copy of it. The pictures (resources.qrc) are compiled into each
# program that links it: in a static library nothing would pull them in.
add_library(rbgui STATIC ${SOURCE} ${OTHER} ${UI} ${MOC})
# The headers uic writes from the .ui files are included by the window's own
# headers, so whatever links rbgui needs to find them too: include_<config>
# with a generator that builds several configurations, include without.
get_property(RB_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(RB_MULTI_CONFIG)
  set(RB_GUI_UI_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/rbgui_autogen/include_$<CONFIG>")
else()
  set(RB_GUI_UI_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/rbgui_autogen/include")
endif()
target_include_directories(rbgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${RB_GUI_UI_HEADERS}")
target_link_libraries(rbgui PUBLIC
    rbcore
    Qt6::Widgets
    Qt6::Network
    ${RB_MULTIMEDIA_LIB}
)
if(APPLE)
  target_link_libraries(rbgui PUBLIC
      ${COCOA_LIB}
      ${IOKKIT_LIB}
      ${USER_NOTIFICATIONS_LIB}
      ${UNIFORM_TYPE_IDENTIFIERS_LIB}
  )
endif()
target_precompile_headers(rbgui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

# ---- Output target ----
if(WIN32)
  add_executable(RcloneBrowser WIN32 main.cpp resources.rc ${QRC})
  target_link_libraries(RcloneBrowser PRIVATE rbgui)
  target_precompile_headers(RcloneBrowser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
elseif(APPLE)
  set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  add_executable(rclone-browser MACOSX_BUNDLE main.cpp ${QRC} icon.icns)
  target_link_libraries(rclone-browser PRIVATE rbgui)
  target_precompile_headers(rclone-browser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
  set_target_properties(rclone-browser PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )
else()
  add_executable(rclone-browser main.cpp ${QRC})
  target_link_libraries(rclone-browser PRIVATE rbgui)
  target_precompile_headers(rclone-browser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

  install(TARGETS rclone-browser RUNTIME DESTINATION bin)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.svg" DESTINATION "share/icons/hicolor/scalable/apps")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.desktop" DESTINATION "share/applications")
endif()

# ---- The window's languages ----
# Every translations/rclonebrowser_<language>.ts, compiled into :/i18n/ of the
# program. None yet: the window is in English until there is one. The
# update_translations target (lupdate) refreshes them from the code and the
# .ui files. See docs/TRANSLATING.md.
if(WIN32)
  set(RB_WINDOW_TARGET RcloneBrowser)
else()
  set(RB_WINDOW_TARGET rclone-browser)
endif()
file(GLOB RB_TS_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/translations/rclonebrowser_*.ts")
if(Qt6LinguistTools_FOUND AND RB_TS_FILES)
  # The words are in rbgui now; lupdate is told where to read them.
  qt_add_translations(${RB_WINDOW_TARGET}
      TS_FILES ${RB_TS_FILES}
      SOURCES ${SOURCE} ${UI} main.cpp
      RESOURCE_PREFIX /i18n
      LUPDATE_OPTIONS -no-obsolete)
endif()

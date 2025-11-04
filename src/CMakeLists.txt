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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

if(APPLE)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(IOKKIT_LIB IOKit REQUIRED)
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
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  list_of_job_options.h
  item_model.h
  qcron.h
  scheduler_widget.h
  hours_spinbox.h
  minutes_spinbox.h
  delete_progress_dialog.h
  file_dialog.h
  remote_folder_dialog.h
)

set(OTHER
  pch.h
  utils.h
  job_options.h
  global.h
  qcronfield.h
  qcronnode.h
)

set(SOURCE
  pch.cpp
  main.cpp
  main_window.cpp
  remote_widget.cpp
  transfer_dialog.cpp
  mount_dialog.cpp
  export_dialog.cpp
  check_dialog.cpp
  dedupe_dialog.cpp
  progress_dialog.cpp
  job_widget.cpp
  mount_widget.cpp
  stream_widget.cpp
  preferences_dialog.cpp
  icon_cache.cpp
  item_model.cpp
  utils.cpp
  job_options.cpp
  list_of_job_options.cpp
  qcron.cpp
  qcronfield.cpp
  qcronnode.cpp
  scheduler_widget.cpp
  hours_spinbox.cpp
  minutes_spinbox.cpp
  delete_progress_dialog.cpp
  file_dialog.cpp
  remote_folder_dialog.cpp
)

if(WIN32)
  set(OTHER ${OTHER} resources.rc)
elseif(APPLE)
  set(OTHER ${OTHER} osx_helper.h mac_os_notifications.h mac_os_power_saving.h)
  set(SOURCE ${SOURCE} osx_helper.mm mac_os_notifications.mm mac_os_power_saving.mm)
endif()

set(QRC resources.qrc)

add_definitions(-DRCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")

# ---- Output target ----
if(WIN32)
  add_executable(RcloneBrowser WIN32 ${SOURCE} ${OTHER} ${UI} ${MOC} ${QRC})
  target_link_libraries(RcloneBrowser PRIVATE
      Qt6::Widgets
      Qt6::Network
      Qt6::Multimedia
  )
  target_precompile_headers(RcloneBrowser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
elseif(APPLE)
  set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  add_executable(rclone-browser MACOSX_BUNDLE ${SOURCE} ${OTHER} ${UI} ${MOC} ${QRC} icon.icns)
  target_link_libraries(rclone-browser PRIVATE
      Qt6::Widgets
      Qt6::Network
      Qt6::Multimedia
      ${COCOA_LIB}
      ${IOKKIT_LIB}
  )
  set_target_properties(rclone-browser PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )
  target_precompile_headers(rclone-browser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
else()
  add_executable(rclone-browser ${SOURCE} ${OTHER} ${UI} ${MOC} ${QRC})
  target_link_libraries(rclone-browser PRIVATE
      Qt6::Widgets
      Qt6::Network
      Qt6::Multimedia
  )

  install(TARGETS rclone-browser RUNTIME DESTINATION bin)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.svg" DESTINATION "share/icons/hicolor/scalable/apps")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.desktop" DESTINATION "share/applications")
  target_precompile_headers(rclone-browser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
endif()

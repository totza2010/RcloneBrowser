find_package(Qt6 REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

# A stand-in for rclone, so the code that runs it can be tested without one.
# Built with the tests and found through RB_FAKE_RCLONE.
add_executable(fake_rclone fake_rclone.cpp)

# Tests link rbcore only. If a test ever needs Qt6::Widgets to link, something
# that belongs in the GUI has drifted into the core.
# Qt's own plugins, not whatever sits beside the tests: building the program
# copies Qt into the same folder with only the "windows" platform plugin, and
# offscreen is then not found -- every test that opens a window aborted.
set(RB_TEST_ENV "QT_QPA_PLATFORM=offscreen")
if(QT6_INSTALL_PREFIX AND QT6_INSTALL_PLUGINS)
  list(APPEND RB_TEST_ENV "QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
endif()

function(rb_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE rbcore Qt6::Test)
  target_compile_definitions(${name} PRIVATE
      RB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
      RB_FAKE_RCLONE="$<TARGET_FILE:fake_rclone>")
  add_dependencies(${name} fake_rclone)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${RB_TEST_ENV}")
endfunction()

rb_add_test(test_rclone_capabilities)
rb_add_test(test_rclone_flags)
rb_add_test(test_redact_args)
rb_add_test(test_utils)
rb_add_test(test_job_stats)
rb_add_test(test_task_store)
rb_add_test(test_task_runner)
rb_add_test(test_job_registry)
rb_add_test(test_lsjson_parser)
rb_add_test(test_job_log)
rb_add_test(test_run_history)
rb_add_test(test_config_store)
rb_add_test(test_job_queue)
rb_add_test(test_scheduler_store)
rb_add_test(test_app_settings)
rb_add_test(test_remote_registry)
rb_add_test(test_debug_log)
rb_add_test(test_script_runner)
rb_add_test(test_rclone_version)
rb_add_test(test_task_builder)
rb_add_test(test_directory_listing)
rb_add_test(test_app_core)
rb_add_test(test_http_message)
rb_add_test(test_api_server)
rb_add_test(test_api_auth)
rb_add_test(test_api_events)
rb_add_test(test_shutdown)
rb_add_test(test_remote_ops)
rb_add_test(test_local_files)
rb_add_test(test_task_json)
rb_add_test(test_extra_flags)
rb_add_test(test_schedule_json)
rb_add_test(test_rclone_config)

# The window, clicked through: the one kind of test that links Qt6::Widgets,
# and so the one that needs rbgui -- the window's own code, which the
# program links too. Absent from a build without the window.
if(TARGET rbgui)
  set(CMAKE_AUTORCC ON)
  add_executable(test_window test_window.cpp ../src/resources.qrc)
  target_link_libraries(test_window PRIVATE rbgui Qt6::Test)
  target_precompile_headers(test_window PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_compile_definitions(test_window PRIVATE
      RB_FAKE_RCLONE="$<TARGET_FILE:fake_rclone>")
  if(QT6_INSTALL_PREFIX AND QT6_INSTALL_PLUGINS)
    target_compile_definitions(test_window PRIVATE
        RB_QT_PLUGINS="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
  endif()
  add_dependencies(test_window fake_rclone)
  add_test(NAME test_window COMMAND test_window)
  set_tests_properties(test_window PROPERTIES ENVIRONMENT "${RB_TEST_ENV}")
endif()

# Not registered as a test: run by hand to regenerate the golden task file
# after JobOptions changes shape. See the comment at the top of the source.
add_executable(make_task_fixture EXCLUDE_FROM_ALL make_task_fixture.cpp)
target_link_libraries(make_task_fixture PRIVATE rbcore)
target_compile_definitions(make_task_fixture PRIVATE
    RB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

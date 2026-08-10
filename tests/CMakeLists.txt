find_package(Qt6 REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

# Tests link rbcore only. If a test ever needs Qt6::Widgets to link, something
# that belongs in the GUI has drifted into the core.
function(rb_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE rbcore Qt6::Test)
  target_compile_definitions(${name} PRIVATE
      RB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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

# Not registered as a test: run by hand to regenerate the golden task file
# after JobOptions changes shape. See the comment at the top of the source.
add_executable(make_task_fixture EXCLUDE_FROM_ALL make_task_fixture.cpp)
target_link_libraries(make_task_fixture PRIVATE rbcore)
target_compile_definitions(make_task_fixture PRIVATE
    RB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

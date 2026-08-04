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
rb_add_test(test_redact_args)
rb_add_test(test_utils)
rb_add_test(test_job_stats)

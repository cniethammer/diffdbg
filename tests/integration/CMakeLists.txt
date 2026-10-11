# Integration tests with bats
find_program(BATS_COMMAND bats)
if(BATS_COMMAND)
    # Add integration test target - run all .bats files in the directory
    add_test(NAME integration_tests COMMAND ${BATS_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR})
    # Mark as external test since bats runs its own discovery
    set_tests_properties(integration_tests PROPERTIES
        FIXTURES_REQUIRED bats_setup
        TIMEOUT 60
        ENVIRONMENT "EXAMPLE_DIR=${CMAKE_BINARY_DIR}/examples/"
    )
else()
    message(STATUS "bats not found - integration tests will be skipped")
endif()

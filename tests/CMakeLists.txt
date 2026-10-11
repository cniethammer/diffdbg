# Unit tests with Catch2
option(BUILD_UNIT_TESTS "Build unit tests" ON)

if(BUILD_UNIT_TESTS)
    add_subdirectory(unit)
endif()

# Integration tests with bats
option(BUILD_INTEGRATION_TESTS "Build integration tests" ON)

if(BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()

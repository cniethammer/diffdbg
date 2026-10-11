# Unit tests with Catch2
find_package(Catch2 REQUIRED)

# Unit test executable
add_executable(diffdbg_tests
    test_units.cpp
    tests_computeStats.cpp
)

target_include_directories(diffdbg_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples
)

target_link_libraries(diffdbg_tests PRIVATE
    diffdbg::diffdbg
    Catch2::Catch2WithMain
)

add_test(NAME diffdbg_tests COMMAND diffdbg_tests)

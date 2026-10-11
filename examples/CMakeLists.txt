# Build example programs
add_executable(example example.cpp)
add_executable(example-2 example-2.cpp)

# Link against the header-only library (include directory)
target_include_directories(example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(example-2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# C++17 standard
set_target_properties(example example-2 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

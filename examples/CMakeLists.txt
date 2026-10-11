# Build example programs
add_executable(example example.cpp)
add_executable(example-2 example-2.cpp)

# Link against the diffdbg library target
target_link_libraries(example PRIVATE diffdbg::diffdbg)
target_link_libraries(example-2 PRIVATE diffdbg::diffdbg)

# C++17 standard
set_target_properties(example example-2 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

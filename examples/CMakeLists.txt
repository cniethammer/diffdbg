# Build example programs
add_executable(example example.cpp)
add_executable(scoped scoped.cpp)
add_executable(precision precision.cpp)

# Link against the diffdbg library target
target_link_libraries(example PRIVATE diffdbg::diffdbg)
target_link_libraries(scoped PRIVATE diffdbg::diffdbg)
target_link_libraries(precision PRIVATE diffdbg::diffdbg)

# C++17 standard
set_target_properties(example scoped precision PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

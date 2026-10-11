cmake_minimum_required(VERSION 3.10)
project(diffdbg VERSION 1.0.0 LANGUAGES CXX)

# Build examples (enabled by default)
option(BUILD_EXAMPLES "Build example programs" ON)

# Header-only library target
add_library(diffdbg INTERFACE)
target_include_directories(diffdbg INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Create alias for use in subdirectories
add_library(diffdbg::diffdbg ALIAS diffdbg)

# Install the header file
install(FILES diffdbg.hpp DESTINATION include)

# Install and export the library target
install(TARGETS diffdbg EXPORT diffdbgTargets)
install(EXPORT diffdbgTargets DESTINATION lib/cmake/diffdbg NAMESPACE diffdbg::)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

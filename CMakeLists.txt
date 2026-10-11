cmake_minimum_required(VERSION 3.10)
project(diffdbg VERSION 1.0.0 LANGUAGES CXX)

# Header-only library - no compilation needed
# The header file diffdbg.hpp is the main deliverable

# Install the header file
install(FILES diffdbg.hpp DESTINATION include)

# Build examples (enabled by default)
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

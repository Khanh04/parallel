cmake_minimum_required(VERSION 3.13.4)
project(phase1-tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages for the test environment
find_package(MPI REQUIRED)

# Include directories
include_directories(${MPI_INCLUDE_PATH})
include_directories(..)

# Create the test executable
add_executable(phase1-tests 
    run_phase1_tests.cpp
)

# Link MPI libraries (for compilation testing)
target_link_libraries(phase1-tests ${MPI_LIBRARIES})

# Add compiler flags
target_compile_options(phase1-tests PRIVATE 
    -fopenmp
    -Wno-unused-parameter
    -Wno-strict-aliasing
)

# Custom target to run tests
add_custom_target(run-tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/phase1-tests
    DEPENDS phase1-tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running Phase 1 unit tests"
)
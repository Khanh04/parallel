cmake_minimum_required(VERSION 3.13.4)
project(mpi-parallelizer)

# Find required packages
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# Include LLVM/Clang directories
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Create the executable
add_executable(mpi-parallelizer mpi_parallelizer.cpp)

# Link against Clang libraries
target_link_libraries(mpi-parallelizer
    clangAST
    clangBasic
    clangDriver
    clangFrontend
    clangRewriteFrontend
    clangSerialization
    clangTooling
    clangRewrite
)

# Link against LLVM libraries
target_link_libraries(mpi-parallelizer ${LLVM_AVAILABLE_LIBS})

# Set C++ standard to C++17 (required for modern LLVM)
set_target_properties(mpi-parallelizer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add compiler-specific flags
if(APPLE)
    # On macOS, ensure we use the right standard library
    target_compile_options(mpi-parallelizer PRIVATE -stdlib=libc++)
    target_link_options(mpi-parallelizer PRIVATE -stdlib=libc++)
endif()

# Disable problematic warnings that can cause issues with LLVM headers
target_compile_options(mpi-parallelizer PRIVATE
    -Wno-unused-parameter
    -Wno-strict-aliasing
)
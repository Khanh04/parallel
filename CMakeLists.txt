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

# Create the executable with modular source files
add_executable(mpi-parallelizer 
    mpi_parallelizer_new.cpp
    data_structures.h
    loop_analyzer.cpp
    function_analyzer.cpp
    main_extractor.cpp
    hybrid_parallelizer.cpp
    ast_consumer.cpp
)

# Original monolithic version removed - using modular architecture only

# Link against Clang libraries for the modular version
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

# Get the specific LLVM libraries we need (excluding MLIR)
llvm_map_components_to_libnames(llvm_libs 
    support 
    core 
    irreader 
    analysis 
    transformutils 
    instcombine 
    scalaropts 
    objcarcopts 
    ipo 
    vectorize 
    option
    mcparser
    mc
    bitwriter
    bitreader
)

# Link against specific LLVM libraries
target_link_libraries(mpi-parallelizer ${llvm_libs})

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
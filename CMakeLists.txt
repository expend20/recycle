cmake_minimum_required(VERSION 3.14)
project(remill_test)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Find Remill
find_package(remill REQUIRED CONFIG)

# Find gflags
set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags REQUIRED CONFIG)

# Add executable
add_executable(remill_test test.cpp)

# Include directories
target_include_directories(remill_test PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${REMILL_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(remill_test PRIVATE
    remill
    ${LLVM_LIBRARIES}
    gflags::gflags
)

# Set C++ standard
set_target_properties(remill_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
) 
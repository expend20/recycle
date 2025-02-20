cmake_minimum_required(VERSION 3.14)

# Set Clang as the compiler
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(remill_test)

# Enable verbose output
set(CMAKE_VERBOSE_MAKEFILE ON)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Add LLVM definitions and include directories globally
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# Get proper LLVM library list
llvm_map_components_to_libnames(llvm_libs
    core
    support
    irreader
    executionengine
    interpreter
    mcjit
    x86codegen
    x86asmparser
    x86desc
    x86info
    native
)

# Find Remill
find_package(remill REQUIRED CONFIG)

# Find gflags
set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags REQUIRED CONFIG)

# Find XED from vcpkg
find_package(XED CONFIG REQUIRED)
message(STATUS "Found XED ${XED_VERSION}")

# Log XED library information
get_target_property(XED_LIB XED::XED IMPORTED_LOCATION_RELEASE)
get_target_property(XED_ILD_LIB XED::ILD IMPORTED_LOCATION_RELEASE)
get_target_property(XED_INCLUDE XED::XED INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "XED library: ${XED_LIB}")
message(STATUS "XED ILD library: ${XED_ILD_LIB}")
message(STATUS "XED include: ${XED_INCLUDE}")

# Custom commands to generate LLVM IR in readable format
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/Utils.ll
    COMMAND ${CMAKE_CXX_COMPILER} -S -emit-llvm 
            -I${REMILL_INCLUDE_DIRS}
            -I${CMAKE_SOURCE_DIR}/src/include
            ${CMAKE_SOURCE_DIR}/src/lib/prebuilt/Utils.cpp 
            -o ${CMAKE_BINARY_DIR}/Utils.ll
    DEPENDS src/lib/prebuilt/Utils.cpp
    COMMENT "Generating readable LLVM IR from Utils.cpp"
)

# Custom target for the LLVM IR files
add_custom_target(prebuilt_ir ALL
    DEPENDS 
        ${CMAKE_BINARY_DIR}/Utils.ll
)

add_executable(remill_test src/test/remill_test.cpp)
add_executable(minidump_test src/test/minidump_test.cpp)
add_executable(llvm_jit_test src/test/llvm_jit_test.cpp)

# Add recycle executable
add_executable(recycle
    src/recycle.cpp
    src/lib/MinidumpContext.cpp
    src/lib/XEDDisassembler.cpp
    src/lib/BasicBlockLifter.cpp
    src/lib/BasicBlockDisassembler.cpp
    src/lib/JITEngine.cpp
    src/lib/JITRuntime.cpp
    src/lib/LoggingPass.cpp
    src/lib/RemoveSuffixPass.cpp
    src/lib/PassManager.cpp
    src/lib/RenamePass.cpp
    src/lib/MiscUtils.cpp
)

# Make recycle depend on the LLVM IR generation
add_dependencies(recycle prebuilt_ir)

# Pass build directory to code
target_compile_definitions(recycle PRIVATE
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

# Include directories
target_include_directories(remill_test PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${REMILL_INCLUDE_DIRS}
)

target_include_directories(minidump_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${XED_INCLUDE}
)

target_include_directories(recycle PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${REMILL_INCLUDE_DIRS}
    ${XED_INCLUDE}
)

target_include_directories(llvm_jit_test PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(remill_test PRIVATE
    remill
    ${LLVM_LIBRARIES}
    gflags::gflags
)

target_link_libraries(minidump_test PRIVATE
    XED::XED
)

target_link_libraries(recycle PRIVATE
    remill
    ${LLVM_LIBRARIES}
    XED::XED
)

target_link_libraries(llvm_jit_test PRIVATE
    ${llvm_libs}
)

# Set C++ standard for both targets
set_target_properties(remill_test minidump_test recycle llvm_jit_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
) 
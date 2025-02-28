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
            ${CMAKE_SOURCE_DIR}/src/lib/Prebuilt/Utils.cpp 
            -o ${CMAKE_BINARY_DIR}/Utils.ll
    DEPENDS src/lib/Prebuilt/Utils.cpp
    COMMENT "Generating readable LLVM IR from Utils.cpp"
)

# Custom target for the LLVM IR files
add_custom_target(prebuilt_ir ALL
    DEPENDS 
        ${CMAKE_BINARY_DIR}/Utils.ll
)

# Custom commands to generate LLVM IR in readable format
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/Utils_opt.ll
    COMMAND ${CMAKE_CXX_COMPILER} -S -emit-llvm 
            -I${REMILL_INCLUDE_DIRS}
            -I${CMAKE_SOURCE_DIR}/src/include
            ${CMAKE_SOURCE_DIR}/src/lib/Prebuilt/Utils_opt.cpp 
            -o ${CMAKE_BINARY_DIR}/Utils_opt.ll
    DEPENDS src/lib/Prebuilt/Utils_opt.cpp
    COMMENT "Generating readable LLVM IR from Utils_opt.cpp"
)

# Custom target for the LLVM IR files
add_custom_target(prebuilt_ir_opt ALL
    DEPENDS 
        ${CMAKE_BINARY_DIR}/Utils_opt.ll
)

add_executable(remill_test src/sample/remill_test.cpp)
add_executable(minidump_test src/sample/minidump_test.cpp)
add_executable(llvm_jit_test src/sample/llvm_jit_test.cpp)

# Create library for core functionality
add_library(recycle_lib
    src/lib/Minidump/MinidumpContext.cpp
    src/lib/Disasm/XEDDisassembler.cpp
    src/lib/Disasm/BasicBlockDisassembler.cpp
    src/lib/Lift/BasicBlockLifter.cpp
    src/lib/JIT/JITEngine.cpp
    src/lib/JIT/JITRuntime.cpp
    src/lib/BitcodeManipulation/InsertLogging.cpp
    src/lib/BitcodeManipulation/RemoveSuffix.cpp
    src/lib/BitcodeManipulation/Rename.cpp
    src/lib/BitcodeManipulation/MiscUtils.cpp
    src/lib/BitcodeManipulation/AddMissingBlockHandler.cpp
    src/lib/BitcodeManipulation/AddMissingMemory.cpp
    src/lib/BitcodeManipulation/AddMissingMemoryHandler.cpp
    src/lib/BitcodeManipulation/MakeSymbolsInternal.cpp
    src/lib/BitcodeManipulation/OptimizeModule.cpp
    src/lib/BitcodeManipulation/ExtractMissingBlocks.cpp
    src/lib/BitcodeManipulation/ReplaceMissingBlockCalls.cpp
)

target_include_directories(recycle_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${REMILL_INCLUDE_DIRS}
    ${XED_INCLUDE}
)

# Add binary dir definition to library
target_compile_definitions(recycle_lib PUBLIC
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

target_link_libraries(recycle_lib PUBLIC
    remill
    ${LLVM_LIBRARIES}
    XED::XED
)

# Add recycle executable
add_executable(recycle
    src/recycle.cpp
)

add_executable(recycle_opt
    src/recycle_opt.cpp
)

# Make recycle depend on the LLVM IR generation
add_dependencies(recycle prebuilt_ir)
add_dependencies(recycle_opt prebuilt_ir_opt)

# Update recycle executable to use the library
target_link_libraries(recycle PRIVATE
    recycle_lib
)

target_link_libraries(recycle_opt PRIVATE
    recycle_lib
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
    ${CMAKE_SOURCE_DIR}/src/lib
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

# Find Google Test
find_package(GTest REQUIRED)

# Add test executable
add_executable(unit_tests
    src/test/AddMissingBlockHandlerTest.cpp
    src/test/MissingMemoryTrackerTest.cpp
    src/test/AddMissingMemoryHandlerTest.cpp
)

# Make unit tests depend on prebuilt_ir
add_dependencies(unit_tests prebuilt_ir)

# Link test executable with Google Test and other dependencies
target_link_libraries(unit_tests PRIVATE
    GTest::GTest
    GTest::Main
    recycle_lib
    gflags::gflags
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${REMILL_INCLUDE_DIRS}
    ${XED_INCLUDE}
)

# Enable testing
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests) 
cmake_minimum_required(VERSION 3.13.4)
project("obfuscation")

############## LLVM CONFIGURATION #################
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LT_LLVM_INSTALL_DIR}")

# Incorporate CMake features provided by LLVM:
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

message("LLVM STATUS:
Definitions ${LLVM_DEFINITIONS}
Includes    ${LLVM_INCLUDE_DIRS}
Libraries   ${LLVM_LIBRARY_DIRS}
Targets     ${LLVM_TARGETS_TO_BUILD}"
)

# Set LLVM header and library paths:
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs support core irreader)

############## PROJ CONFIGURATION #################
# LLVM has its own internal RTTI system, disable cpp-default-rtti:
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# LLVM uses C++-17 standard, ensure cpp parity:
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

add_subdirectory(lib)


#######################################################
############### CLANG CONFIGURATION ###################
find_package(Clang REQUIRED CONFIG)

# set(CT_LLVM_CMAKE_FILE
#   "${CT_Clang_INSTALL_DIR}/lib/cmake/clang/ClangConfig.cmake")
# if(NOT EXISTS "${CT_LLVM_CMAKE_FILE}")
# message(FATAL_ERROR
#   " CT_LLVM_CMAKE_FILE (${CT_LLVM_CMAKE_FILE}) is invalid.")
# endif()

# list(APPEND CMAKE_PREFIX_PATH "${CT_Clang_INSTALL_DIR}/lib/cmake/clang/")

message(STATUS "Found Clang ${LLVM_PACKAGE_VERSION}")

message("CLANG STATUS:
  Includes (clang)    ${CLANG_INCLUDE_DIRS}
  Includes (llvm)     ${LLVM_INCLUDE_DIRS}"
)

# Set the LLVM and Clang header and library paths
include_directories(SYSTEM "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}")

# # Compiler flags
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall\
#     -fdiagnostics-color=always")

# -fvisibility-inlines-hidden is set when building LLVM and on Darwin warnings
# are triggered if llvm-tutor is built without this flag (though otherwise it
# builds fine). For consistency, add it here too.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden"
  SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(${SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG} EQUAL "1")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
endif()

# # Set the build directories
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
cmake_minimum_required(VERSION 3.27)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compilation database")
project(LLVMDomainOptimizations LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

option(ERROR_NAMES "Enable name-based checks when loaded by Clang" ON)
option(BEAST_ERRORS "Enable Beast error assignments when loaded by Clang" OFF)
set(BENCHMARK "" CACHE STRING "Parser benchmark")
option(BUILD_BEAST_BENCHMARK "Build the Beast parser benchmark and integration tests" OFF)

add_llvm_pass_plugin(ErrorHeuristic passes/error/Plugin.cpp passes/error/ErrorHeuristic.cpp)
target_include_directories(ErrorHeuristic SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_COMPILE_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(ErrorHeuristic PRIVATE ${LLVM_COMPILE_DEFINITIONS} -Wall -Wextra)
target_compile_definitions(ErrorHeuristic PRIVATE
  ERROR_NAMES=$<BOOL:${ERROR_NAMES}> BEAST_ERRORS=$<BOOL:${BEAST_ERRORS}>)
target_compile_features(ErrorHeuristic PRIVATE cxx_std_20)

add_llvm_pass_plugin(X86LayoutPolicy passes/x86-layout/X86LayoutPolicy.cpp)
target_include_directories(X86LayoutPolicy SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_options(X86LayoutPolicy PRIVATE ${LLVM_COMPILE_DEFINITIONS})
target_compile_features(X86LayoutPolicy PRIVATE cxx_std_20)

if(BUILD_BEAST_BENCHMARK AND NOT BENCHMARK)
  set(BENCHMARK beast)
endif()
if(BENCHMARK)
  if(NOT BENCHMARK MATCHES "^[a-z0-9-]+$" OR NOT EXISTS "${PROJECT_SOURCE_DIR}/bench/${BENCHMARK}/CMakeLists.txt")
    message(FATAL_ERROR "Unknown BENCHMARK: ${BENCHMARK}")
  endif()
  include(bench/Benchmark.cmake)
  add_subdirectory(bench/${BENCHMARK})
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(INTEGRATION_ARGS)
  if(BENCHMARK STREQUAL "beast")
    list(APPEND INTEGRATION_ARGS --beast "${beast_SOURCE_DIR}" --boost "${Boost_INCLUDE_DIRS}")
  endif()
  add_test(NAME passes COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/run.py"
    --llvm "${LLVM_TOOLS_BINARY_DIR}/.." --plugin $<TARGET_FILE:ErrorHeuristic>
    --out "${PROJECT_BINARY_DIR}/tests" ${INTEGRATION_ARGS})
  if(BENCHMARK MATCHES "^(beast|fix|gzip|pugixml)$")
    add_test(NAME replay COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/benchmark.py"
      ${BENCHMARK} $<TARGET_FILE:${BENCHMARK}-bench>)
  endif()
endif()

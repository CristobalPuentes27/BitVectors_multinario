cmake_minimum_required(VERSION 3.20)

project(adaptive VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ADAPTIVE_NATIVE_OPTIMIZATIONS "Enable -march=native optimizations for the library target" OFF)

add_library(adaptive INTERFACE)

target_compile_features(adaptive INTERFACE cxx_std_20)

target_compile_definitions(adaptive
  INTERFACE
    $<$<CONFIG:Release>:NDEBUG>
)

if(ADAPTIVE_NATIVE_OPTIMIZATIONS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(adaptive
    INTERFACE
      $<$<CONFIG:Release>:-march=native>
  )
endif()

target_include_directories(adaptive
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_sources(adaptive
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/adaptive.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/assert.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_util.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/policy.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/tagged_node_ptr.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/array/array_node_ptr.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/array/array_vector.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/array/dynamic_array_node.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/array/leaf_array_node.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/array/static_array_node.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_vector/bit_vector.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_vector/bv_node_ptr.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_vector/dynamic_bv_node.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_vector/leaf_bv_node.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/adaptive/bit_vector/static_bv_node.hpp>
)

add_library(adaptive::adaptive ALIAS adaptive)

# Installation and packaging
install(TARGETS adaptive
  EXPORT adaptiveTargets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT adaptiveTargets
  FILE adaptiveTargets.cmake
  NAMESPACE adaptive::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/adaptive
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfig.cmake.in"
"@PACKAGE_INIT@

include(\"\${CMAKE_CURRENT_LIST_DIR}/adaptiveTargets.cmake\")

check_required_components(adaptive)
"
)

configure_package_config_file(
  "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/adaptive
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/adaptiveConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/adaptive
)

# Benchmarks
option(ADAPTIVE_BUILD_BENCHMARKS "Build adaptive benchmark binaries" OFF)

if(ADAPTIVE_BUILD_BENCHMARKS)
  foreach(bench_name IN ITEMS access rank select)
    add_executable(adaptive_bench_${bench_name}
      benchmarks/adaptive/${bench_name}.cpp
    )
    target_link_libraries(adaptive_bench_${bench_name}
      PRIVATE
        adaptive::adaptive
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
      target_compile_options(adaptive_bench_${bench_name}
        PRIVATE
          $<$<CONFIG:Release>:-O3>
          $<$<CONFIG:Release>:-march=native>
      )
    endif()
    set_target_properties(adaptive_bench_${bench_name}
      PROPERTIES
        OUTPUT_NAME ${bench_name}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench/adaptive
    )
  endforeach()
endif()

# Tests (correctos con ASan/UBSan; se compilan SIN NDEBUG para conservar los assert de la libreria)
option(ADAPTIVE_BUILD_TESTS "Build adaptive tests" OFF)
option(ADAPTIVE_TEST_SANITIZE "Build tests with AddressSanitizer and UBSan" ON)

if(ADAPTIVE_BUILD_TESTS)
  enable_testing()
  foreach(test_name IN ITEMS bit_util static_bv leaf_bv bit_vector)
    add_executable(adaptive_test_${test_name} tests/test_${test_name}.cpp)
    # No se enlaza contra adaptive::adaptive porque esa interfaz define NDEBUG en Release.
    target_include_directories(adaptive_test_${test_name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(adaptive_test_${test_name} PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
      target_compile_options(adaptive_test_${test_name} PRIVATE -O1 -g -Wall -Wextra)
      if(ADAPTIVE_TEST_SANITIZE)
        target_compile_options(adaptive_test_${test_name} PRIVATE
          -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        target_link_options(adaptive_test_${test_name} PRIVATE -fsanitize=address,undefined)
      endif()
    endif()
    add_test(NAME ${test_name} COMMAND adaptive_test_${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 3000)
  endforeach()

  # Diagnostico (no es un test): costo de memoria del splitting. Sin sanitizers: reemplaza operator new.
  add_executable(adaptive_diag_split_cost tests/diag_split_cost.cpp)
  target_link_libraries(adaptive_diag_split_cost PRIVATE adaptive::adaptive)
  set_target_properties(adaptive_diag_split_cost PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench/adaptive)
endif()

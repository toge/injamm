cmake_minimum_required(VERSION 3.30)
project(injamm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- オプション ----
option(INJAMM_ENABLE_THREADED_DISPATCH "GCC computed goto dispatch (GCC only)" ON)
option(INJAMM_BUILD_TESTS "Build tests" ON)
option(INJAMM_BUILD_EXAMPLES "Build examples" ON)

# ---- 依存 ----
find_package(glaze CONFIG REQUIRED)

# ---- ライブラリ定義 ----
add_library(injamm INTERFACE)
add_library(injamm::injamm ALIAS injamm)

target_include_directories(injamm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(injamm INTERFACE glaze::glaze)
target_compile_features(injamm INTERFACE cxx_std_26)

# 高速化のためのGCCスレッデッドディスパッチ
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(INJAMM_ENABLE_THREADED_DISPATCH)
    target_compile_definitions(injamm INTERFACE INJAMM_THREADED_DISPATCH)
    target_compile_options(injamm INTERFACE -std=gnu++26)
  endif()
endif()

# ---- テスト ----
if(INJAMM_BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)

  enable_testing()
  add_executable(injamm_tests
    tests/test_injamm.cpp
    tests/test_injamm_ct.cpp
  )
  target_link_libraries(injamm_tests PRIVATE injamm Catch2::Catch2WithMain)
  target_include_directories(injamm_tests PRIVATE include)
  target_compile_features(injamm_tests PRIVATE cxx_std_26)
  add_test(NAME injamm_tests COMMAND injamm_tests)
endif()

# ---- サンプル ----
if(INJAMM_BUILD_EXAMPLES)
  add_executable(injamm_example
    examples/example.cpp
  )
  target_link_libraries(injamm_example PRIVATE injamm)
  target_include_directories(injamm_example PRIVATE include)
  target_compile_features(injamm_example PRIVATE cxx_std_26)
endif()

# ---- インストール ----
include(GNUInstallDirs)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)

install(
  TARGETS injamm
  EXPORT injammTargets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT injammTargets
  FILE injammTargets.cmake
  NAMESPACE injamm::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/injammConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/injammConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/injammConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm
)

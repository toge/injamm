cmake_minimum_required(VERSION 3.30)
project(injamm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- オプション ----
option(ENABLE_THREADED_DISPATCH "GCC computed goto dispatch (GCC only)" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_EXAMPLE "Build examples" ON)
option(ENABLE_EXT_SQLITE3 "Build injamm-sqlite3 extension" ON)

# ---- 依存 ----
find_package(glaze CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(FastFloat CONFIG REQUIRED)

# ---- ライブラリ定義 ----
add_library(injamm INTERFACE)
add_library(injamm::injamm ALIAS injamm)

target_include_directories(injamm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(injamm INTERFACE glaze::glaze fmt::fmt FastFloat::fast_float)
target_compile_features(injamm INTERFACE cxx_std_23)

# GCC スレッデッドディスパッチ制御
# ヘッダ側でも自動有効化されるが、CMake から明示的に制御できるよう残す。
# ENABLE_THREADED_DISPATCH=OFF にすると INJAMM_NO_THREADED_DISPATCH を定義して無効化する。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(NOT ENABLE_THREADED_DISPATCH)
    target_compile_definitions(injamm INTERFACE INJAMM_NO_THREADED_DISPATCH)
  else()
    # ヘッダ側の自動検出を補完: gnu++23 拡張を明示的に有効化する
    target_compile_options(injamm INTERFACE -std=gnu++23)
  endif()
else()
  # Clang/MSVC: computed goto 非対応のため switch fallback を使用
  target_compile_definitions(injamm INTERFACE INJAMM_NO_THREADED_DISPATCH)
  # frozenchars に Clang 互換性問題があるため無効化
  target_compile_definitions(injamm INTERFACE INJAMM_NO_FROZENCHARS)
endif()

# ---- Install ----
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

# ---- Tests & Examples ----
if(BUILD_TEST)
  find_package(Catch2 CONFIG REQUIRED)

  enable_testing()
  add_executable(injamm_tests
    tests/test_injamm.cpp
    tests/test_injamm_ct.cpp
    tests/test_injamm_ja.cpp
    tests/test_ct_overload.cpp
  )
  target_link_libraries(injamm_tests PRIVATE injamm Catch2::Catch2WithMain)
  target_include_directories(injamm_tests PRIVATE include)
  target_compile_features(injamm_tests PRIVATE cxx_std_23)

  # Optional: FrozenString NTTP tests (frozenchars header-only library)
  # Clang 互換性問題があるため非 GCC コンパイラではスキップ
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_package(frozenchars CONFIG QUIET)
    if(frozenchars_FOUND)
      target_link_libraries(injamm_tests PRIVATE frozenchars::frozenchars)
      target_compile_definitions(injamm_tests PRIVATE INJAMM_HAS_FROZENCHARS=1)
    endif()
  endif()

  add_test(NAME injamm_tests COMMAND injamm_tests)
endif()

if(BUILD_EXAMPLE)
  add_executable(injamm_example
    examples/example.cpp
  )
  target_link_libraries(injamm_example PRIVATE injamm)
  target_include_directories(injamm_example PRIVATE include)
  target_compile_features(injamm_example PRIVATE cxx_std_23)

  add_executable(injamm_benchmark
    examples/benchmark.cpp
  )
  target_link_libraries(injamm_benchmark PRIVATE injamm)
  target_include_directories(injamm_benchmark PRIVATE include)
  target_compile_features(injamm_benchmark PRIVATE cxx_std_23)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(injamm_benchmark PRIVATE -march=native)
  endif()
endif()

if(ENABLE_EXT_SQLITE3)
  add_subdirectory(ext/injamm-sqlite3)
endif()

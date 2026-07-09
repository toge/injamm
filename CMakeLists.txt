cmake_minimum_required(VERSION 3.30)
project(injamm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 3.30+ tries to find clang-scan-deps for C++20 module support.
# Not needed for this project (traditional headers), and clang-scan-deps
# may not be installed on all platforms (e.g. Fedora 41 clang).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ---- オプション ----
option(ENABLE_THREADED_DISPATCH "GCC computed goto dispatch (GCC only)" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_EXAMPLE "Build examples" ON)
option(BUILD_FUZZ "Build fuzz targets (requires -fsanitize=fuzzer)" OFF)
option(ENABLE_ENUM "Enable enchantum-based enum string conversion" ON)
option(ENABLE_SQLITE3 "Build injamm-sqlite3 extension" ON)

# ---- 依存 ----
find_package(glaze CONFIG REQUIRED)
find_package(FastFloat CONFIG REQUIRED)

if(ENABLE_ENUM)
  # enchantum: vcpkg port の enchantumConfig.cmake が enchantumTargets.cmake を
  # cmake/ サブディレクトリではなく直下に期待するため CONFIG find は使わず、
  # ヘッダパスを直接解決してターゲットを手動作成する。
  # TODO: vcpkg の enchantum port が修正されたら、以下の標準的な find_package に移行してください:
  # find_package(enchantum CONFIG REQUIRED)
  # target_link_libraries(injamm INTERFACE enchantum::enchantum)
  find_path(ENCHANTUM_INCLUDE_DIR enchantum/enchantum.hpp
    PATHS
      "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
      "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
  )
  if(NOT ENCHANTUM_INCLUDE_DIR)
    message(FATAL_ERROR "enchantum headers not found. Please install enchantum via vcpkg.")
  endif()
  if(NOT TARGET enchantum::enchantum)
    add_library(enchantum::enchantum INTERFACE IMPORTED)
    set_target_properties(enchantum::enchantum PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${ENCHANTUM_INCLUDE_DIR}"
      INTERFACE_COMPILE_DEFINITIONS "ENCHANTUM_ENABLE_MSVC_SPEEDUP=$<BOOL:ON>"
      INTERFACE_COMPILE_FEATURES "cxx_std_17"
    )
  endif()
endif()

# ---- ライブラリ定義 ----
add_library(injamm INTERFACE)
add_library(injamm::injamm ALIAS injamm)

target_include_directories(injamm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(ENABLE_ENUM)
  target_link_libraries(injamm INTERFACE glaze::glaze FastFloat::fast_float enchantum::enchantum)
else()
  target_compile_definitions(injamm INTERFACE INJAMM_NO_ENUM_REGISTRY)
  target_link_libraries(injamm INTERFACE glaze::glaze FastFloat::fast_float)
endif()
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
  enable_testing()

  if(EMSCRIPTEN)
    # Emscripten: vcpkg がインストールした Catch2 静的ライブラリは
    # -sNO_DISABLE_EXCEPTION_CATCHING なしでコンパイルされているため、
    # Catch2 の例外翻訳関数が正しく動作しない。
    # ここでは Catch2 ソースをアマルガメーションファイル経由で再コンパイルする。
    include(FetchContent)
    FetchContent_Declare(
      catch2_emscripten
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.15.1
      GIT_SHALLOW TRUE
      SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/catch2_emscripten-src
    )
    cmake_policy(SET CMP0169 OLD)
    FetchContent_GetProperties(catch2_emscripten)
    if(NOT catch2_emscripten_POPULATED)
      FetchContent_Populate(catch2_emscripten)
    endif()
    cmake_policy(SET CMP0169 NEW)
    add_library(catch2_emscripten_core STATIC
      "${catch2_emscripten_SOURCE_DIR}/extras/catch_amalgamated.cpp"
    )
    target_include_directories(catch2_emscripten_core
      BEFORE PUBLIC
        "${catch2_emscripten_SOURCE_DIR}/extras"
    )
    target_compile_options(catch2_emscripten_core
      PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_compile_features(catch2_emscripten_core PRIVATE cxx_std_17)
    target_link_options(catch2_emscripten_core
      INTERFACE "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    set(_catch2_targets catch2_emscripten_core)
    set(_need_custom_main TRUE)
  else()
    find_package(Catch2 CONFIG REQUIRED)
    set(_catch2_targets Catch2::Catch2 Catch2::Catch2WithMain)
    set(_need_custom_main FALSE)
  endif()

  add_executable(injamm_tests
    tests/test_injamm.cpp
    tests/test_injamm_ct.cpp
    tests/test_injamm_ja.cpp
    tests/test_ct_overload.cpp
  )
  if(_need_custom_main)
    target_sources(injamm_tests PRIVATE tests/em_catch2_main.cpp)
  endif()
  target_link_libraries(injamm_tests PRIVATE injamm ${_catch2_targets})
  target_include_directories(injamm_tests PRIVATE include)
  target_compile_features(injamm_tests PRIVATE cxx_std_23)
  if(EMSCRIPTEN)
    target_compile_options(injamm_tests PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING")
  endif()

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

  add_executable(injamm_example_zip
    examples/example_zip.cpp
  )
  target_link_libraries(injamm_example_zip PRIVATE injamm)
  target_include_directories(injamm_example_zip PRIVATE include)
  target_compile_features(injamm_example_zip PRIVATE cxx_std_23)

  add_executable(injamm_benchmark
    examples/benchmark.cpp
  )
  target_link_libraries(injamm_benchmark PRIVATE injamm)
  target_include_directories(injamm_benchmark PRIVATE include)
  target_compile_features(injamm_benchmark PRIVATE cxx_std_23)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(injamm_benchmark PRIVATE -march=native)
  endif()

  add_executable(injamm_bench_delegate
    examples/bench_delegate.cpp
  )
  target_link_libraries(injamm_bench_delegate PRIVATE injamm)
  target_include_directories(injamm_bench_delegate PRIVATE include)
  target_compile_features(injamm_bench_delegate PRIVATE cxx_std_23)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(injamm_bench_delegate PRIVATE -march=native)
  endif()
endif()

# ---- Fuzz targets ----
if(BUILD_FUZZ)
  add_executable(injamm_fuzz fuzz/injamm_fuzz.cpp)
  target_link_libraries(injamm_fuzz PRIVATE injamm)
  target_include_directories(injamm_fuzz PRIVATE include)
  target_compile_features(injamm_fuzz PRIVATE cxx_std_23)
  # Clang: libFuzzer; GCC: standalone mode (iterate over file arguments)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(injamm_fuzz PRIVATE -fsanitize=fuzzer -fno-sanitize-recover=all)
    target_link_options(injamm_fuzz PRIVATE -fsanitize=fuzzer)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
      target_link_libraries(injamm_fuzz PRIVATE -framework CoreFoundation)
    endif()
  else()
    target_compile_definitions(injamm_fuzz PRIVATE INJAMM_FUZZ_STANDALONE)
  endif()
endif()

if(ENABLE_SQLITE3)
  add_subdirectory(ext/injamm-sqlite3)
endif()

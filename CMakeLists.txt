cmake_minimum_required(VERSION 3.30)
project(injamm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ---- オプション ----
option(ENABLE_THREADED_DISPATCH "GCC computed goto dispatch (GCC only)" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_EXAMPLE "Build examples" ON)
option(BUILD_FUZZ "Build fuzz targets (requires -fsanitize=fuzzer)" OFF)
option(ENABLE_ENUM "Enable enchantum-based enum string conversion" ON)
option(BUILD_UTIL "Build CLI utilities" OFF)
option(ENABLE_WASI_MINIMAL "Enable WASI minimal (no exceptions) mode - wasi-sdk wasm32-wasip1 assumed" OFF)
# Apple Clang の libc++ は std::vformat("{:05}", ...) 等の 0 flag を
# 正しくパースできない既知の不具合がある。fmt を使うことで回避する。
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  set(_USE_FMT_DEFAULT ON)
else()
  set(_USE_FMT_DEFAULT OFF)
endif()
option(USE_FMT "Use fmt instead of std::vformat for format filter (workaround for libc++ bugs)" ${_USE_FMT_DEFAULT})

if(ENABLE_WASI_MINIMAL)
  if(USE_FMT)
    message(STATUS "ENABLE_WASI_MINIMAL=ON: disabling USE_FMT (requires exceptions)")
  endif()
  set(USE_FMT OFF)
  if(ENABLE_ENUM)
    message(STATUS "ENABLE_WASI_MINIMAL=ON: disabling ENABLE_ENUM (requires exceptions)")
  endif()
  set(ENABLE_ENUM OFF)
endif()

# ---- 依存 ----
find_package(glaze CONFIG REQUIRED)
if(USE_FMT)
  find_package(fmt CONFIG REQUIRED)
endif()

if(ENABLE_ENUM)
  find_package(enchantum CONFIG REQUIRED)
endif()

# ---- ライブラリ定義 ----
add_library(injamm INTERFACE)
add_library(injamm::injamm ALIAS injamm)

target_include_directories(injamm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(ENABLE_ENUM)
  target_link_libraries(injamm INTERFACE glaze::glaze enchantum::enchantum)
else()
  target_compile_definitions(injamm INTERFACE INJAMM_NO_ENUM_REGISTRY)
  target_link_libraries(injamm INTERFACE glaze::glaze)
endif()
if(USE_FMT)
  target_link_libraries(injamm INTERFACE fmt::fmt)
  target_compile_definitions(injamm INTERFACE INJAMM_USE_FMT)
endif()
target_compile_features(injamm INTERFACE cxx_std_23)

if(ENABLE_WASI_MINIMAL)
  target_compile_definitions(injamm INTERFACE INJAMM_WASI_MINIMAL=1 INJAMM_NO_EXCEPTIONS)
  target_compile_options(injamm INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endif()

# GCC スレッデッドディスパッチ制御
# ヘッダ側でも自動有効化されるが、CMake から明示的に制御できるよう残す。
# ENABLE_THREADED_DISPATCH=OFF にすると INJAMM_NO_THREADED_DISPATCH を定義して無効化する。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(NOT ENABLE_THREADED_DISPATCH)
    target_compile_definitions(injamm INTERFACE INJAMM_NO_THREADED_DISPATCH)
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

# ---- WASI minimal スモークテスト ----
# 全構成で常時ビルドする。INJAMM_WASI_MINIMAL ガード下でもヘッダがコンパイルでき、
# 維持機能（NTTP / engine / フィルタ / セクション / span ベースバイトコード I/O）が
# 動作することを自己検証する。
enable_testing()
add_executable(test_wasi_minimal tests/test_wasi_minimal.cpp)
target_link_libraries(test_wasi_minimal PRIVATE injamm)
target_include_directories(test_wasi_minimal PRIVATE include)
target_compile_features(test_wasi_minimal PRIVATE cxx_std_23)
target_compile_definitions(test_wasi_minimal PRIVATE INJAMM_WASI_MINIMAL)
add_test(NAME wasi_minimal_smoke COMMAND test_wasi_minimal)

# ---- Tests & Examples ----
if(BUILD_TEST AND ENABLE_WASI_MINIMAL)
  message(STATUS "ENABLE_WASI_MINIMAL=ON: Catch2 tests require exceptions -- disabling BUILD_TEST (wasi_minimal_smoke remains)")
  set(BUILD_TEST OFF)
endif()
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
    tests/test_ct_exec_crosscheck.cpp
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
  if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND NOT CMAKE_SYSTEM_NAME MATCHES "WASI|Emscripten")
    target_compile_options(injamm_benchmark PRIVATE -march=native)
  endif()

  add_executable(injamm_codegen_bench
    examples/codegen_bench/bench_codegen.cpp
  )
  target_link_libraries(injamm_codegen_bench PRIVATE injamm)
  target_include_directories(injamm_codegen_bench PRIVATE include examples/codegen_bench)
  target_compile_features(injamm_codegen_bench PRIVATE cxx_std_23)
  if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND NOT CMAKE_SYSTEM_NAME MATCHES "WASI|Emscripten")
    target_compile_options(injamm_codegen_bench PRIVATE -march=native)
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

# テンプレート→バイトコード変換 CLI ツール
if(BUILD_UTIL)
  add_executable(injamm_bc
    util/injamm_bc.cpp
  )
  target_link_libraries(injamm_bc PRIVATE injamm)
  target_include_directories(injamm_bc PRIVATE include)
  target_compile_features(injamm_bc PRIVATE cxx_std_23)

  # バイトコード→C++ コードジェネレーター（glaze 非依存）
  add_executable(injamm_codegen
    util/injamm_codegen.cpp
  )
  target_link_libraries(injamm_codegen PRIVATE injamm)
  target_include_directories(injamm_codegen PRIVATE include)
  target_compile_features(injamm_codegen PRIVATE cxx_std_23)

  # codegen 動作テスト
  if(BUILD_TEST)
    add_executable(test_codegen
      test_codegen/test_codegen.cpp
    )
    target_link_libraries(test_codegen PRIVATE injamm)
    target_include_directories(test_codegen PRIVATE include test_codegen)
    target_compile_features(test_codegen PRIVATE cxx_std_23)
    add_test(NAME codegen_test COMMAND test_codegen)


    add_executable(test_ct_if
      util/test_ct_if.cpp
    )
    target_link_libraries(test_ct_if PRIVATE injamm)
    target_include_directories(test_ct_if PRIVATE include)
    target_compile_features(test_ct_if PRIVATE cxx_std_23)

    add_executable(test_codegen_ext
      test_codegen/test_codegen_extended.cpp
    )
    target_link_libraries(test_codegen_ext PRIVATE injamm)
    target_include_directories(test_codegen_ext PRIVATE include test_codegen)
    target_compile_features(test_codegen_ext PRIVATE cxx_std_23)
    add_test(NAME codegen_ext_test COMMAND test_codegen_ext)
  endif()

  install(
    TARGETS injamm_bc injamm_codegen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

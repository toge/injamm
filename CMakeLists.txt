cmake_minimum_required(VERSION 3.30)
project(injamm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ---- オプション ----
# 例外は既定で有効。wasip1 ビルド時はユーザーが -fno-exceptions を直接指定する。
# (frozenchars と同様の方針: ライブラリは例外なしでも動作するが、オプションは不要)
option(ENABLE_THREADED_DISPATCH "GCC computed goto dispatch (GCC only)" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_EXAMPLE "Build examples" ON)
option(BUILD_FUZZ "Build fuzz targets (requires -fsanitize=fuzzer)" OFF)
option(ENABLE_ENUM "Enable enchantum-based enum string conversion" ON)
option(BUILD_UTIL "Build CLI utilities" OFF)
# Apple Clang の libc++ は std::vformat("{:05}", ...) 等の 0 flag を
# 正しくパースできない既知の不具合がある。fmt を使うことで回避する。
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  set(_USE_FMT_DEFAULT ON)
else()
  set(_USE_FMT_DEFAULT OFF)
endif()
option(USE_FMT "Use fmt instead of std::vformat for format filter (workaround for libc++ bugs)" ${_USE_FMT_DEFAULT})


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

# ---- WASI: ctest が .wasm を直接実行できないため wasmtime 経由にする ----
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  find_program(WASMTIME_EXECUTABLE wasmtime HINTS "$ENV{HOME}/.wasmtime/bin")
  if(WASMTIME_EXECUTABLE)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${WASMTIME_EXECUTABLE};run")
  endif()
endif()

# ---- 例外なしスモークテスト ----
# ライブラリが -fno-exceptions でも動作することを検証する。
# wasip1 ビルド時はユーザーが -fno-exceptions を直接指定する（frozenchars と同様）。
# add_test を有効にするため enable_testing() は BUILD_TEST の外に置く
# （WASI は BUILD_TEST=OFF でも test_no_exceptions を ctest から実行する）。
enable_testing()
add_executable(test_no_exceptions tests/test_no_exceptions.cpp)
target_link_libraries(test_no_exceptions PRIVATE injamm)
target_include_directories(test_no_exceptions PRIVATE include)
target_compile_features(test_no_exceptions PRIVATE cxx_std_23)
target_compile_options(test_no_exceptions PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions>)
add_test(NAME no_exceptions_smoke COMMAND test_no_exceptions)

# ---- Tests & Examples ----
# WASI: Catch2 は例外が必須のため使用不可。vcpkg.json でも catch2 を
# `emscripten | !wasm32` に限定している。WASI ビルドは Catch2 不要の
# test_no_exceptions のみ実行する。
if(BUILD_TEST)
  # WASI では Catch2 テストをスキップし test_no_exceptions のみ使う。
  # (Catch2 は例外必須、vcpkg.json でも wasm32 を除外済み)
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(STATUS "WASI: skipping Catch2 tests (use test_no_exceptions)")
  elseif(EMSCRIPTEN)
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

  if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_executable(injamm_tests
      tests/test_injamm.cpp
      tests/test_injamm_ct.cpp
      tests/test_injamm_ja.cpp
      tests/test_ct_overload.cpp
      tests/test_ct_exec_crosscheck.cpp
      tests/test_injamm_tla_counterexample.cpp
      tests/test_noexcept.cpp
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

  # WASI サーバサイド描画デモ: argv テンプレートを実行時コンパイル→描画 (wasmtime で実行)
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_executable(injamm_wasi_render
      examples/wasi_render.cpp
    )
    target_link_libraries(injamm_wasi_render PRIVATE injamm)
    target_include_directories(injamm_wasi_render PRIVATE include)
    target_compile_features(injamm_wasi_render PRIVATE cxx_std_23)
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
    add_test(NAME ct_if_test COMMAND test_ct_if)

    add_executable(test_codegen_ext
      test_codegen/test_codegen_extended.cpp
    )
    target_link_libraries(test_codegen_ext PRIVATE injamm)
    target_include_directories(test_codegen_ext PRIVATE include test_codegen)
    target_compile_features(test_codegen_ext PRIVATE cxx_std_23)
    add_test(NAME codegen_ext_test COMMAND test_codegen_ext)

    add_executable(test_codegen_nosimd
      test_codegen/test_codegen_nosimd.cpp
    )
    target_link_libraries(test_codegen_nosimd PRIVATE injamm)
    target_include_directories(test_codegen_nosimd PRIVATE include test_codegen)
    target_compile_features(test_codegen_nosimd PRIVATE cxx_std_23)
    add_test(NAME codegen_nosimd_test COMMAND test_codegen_nosimd)

    add_executable(test_codegen_badbc
      test_codegen/test_codegen_badbc.cpp
    )
    target_link_libraries(test_codegen_badbc PRIVATE injamm)
    target_include_directories(test_codegen_badbc PRIVATE include)
    target_compile_features(test_codegen_badbc PRIVATE cxx_std_23)
    add_test(NAME codegen_badbc_test COMMAND test_codegen_badbc $<TARGET_FILE:injamm_codegen>)

    # codegen ライブテスト: 各ケースのヘッダをビルド時に injamm_codegen で生成し、
    # コンパイル（生成ヘッダが壊れていればビルドが失敗する）・実行して VM と比較する。
    # 同梱 render*.hpp をリンクするだけの test_codegen では検出できない回帰を拾う。
    set(CG_LIVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/codegen_cases)
    file(MAKE_DIRECTORY ${CG_LIVE_DIR})
    function(add_codegen_case name tmpl)
      set(out ${CG_LIVE_DIR}/case_${name}.hpp)
      add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:injamm_codegen> -e "${tmpl}" -t CaseData -p case_${name} -n generated -o ${out}
        DEPENDS injamm_codegen
        VERBATIM)
      set(CG_LIVE_HEADERS ${CG_LIVE_HEADERS} ${out} PARENT_SCOPE)
    endfunction()
    add_codegen_case(inv_bool            "{{^active}}x{{/active}}")
    add_codegen_case(inv_int             "{{^age}}zero{{/age}}")
    add_codegen_case(inv_opt             "{{^opt}}none{{/opt}}")
    add_codegen_case(inv_container       "{{^items}}E{{/items}}")
    add_codegen_case(inv_loop_rootkey    "{{#items}}{{^active}}x{{/active}}{{/items}}")
    add_codegen_case(inv_loop_elemkey    "{{#items}}{{^on}}y{{/on}}{{/items}}")
    add_codegen_case(inv_else            "{{^active}}off{{else}}on{{/active}}")
    add_codegen_case(sec_else_container  "{{#items}}{{name}}{{else}}empty{{/items}}")
    add_codegen_case(sec_else_scalar     "{{#active}}Y{{else}}N{{/active}}")
    add_codegen_case(sec_else_int        "{{#age}}N{{else}}Z{{/age}}")
    add_codegen_case(sec_else_in_loop    "{{#items}}{{#on}}T{{else}}F{{/on}}{{/items}}")
    add_codegen_case(opt_section         "{{#opt}}[{{this}}]{{/opt}}")
    add_codegen_case(opt_else            "{{#opt}}[{{this}}]{{else}}none{{/opt}}")
    add_codegen_case(at_else             "{{#items}}{{#loop.is_first}}F{{/loop.is_first}}{{else}}E{{/items}}")
    add_codegen_case(take_else           "{{#items | take(1)}}{{name}}{{else}}E{{/items}}")
    add_codegen_case(cmp_eq_var          "{{#if qty == limit}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_ne_var          "{{#if qty != limit}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_gt_literal      "{{#if qty > 2}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_gte_literal     "{{#if qty >= 3}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_lt_literal      "{{#if qty < 4}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_lte_literal     "{{#if qty <= 3}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_string_var      "{{#if name == alias}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_string_literal  "{{#if name == \"R\"}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_empty_string    "{{#if name == \"\"}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_this_string     "{{#tags}}{{#if this == \"t1\"}}Y{{else}}N{{/if}}{{/tags}}")
    add_codegen_case(cmp_loop_index      "{{#items}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_loop_index1     "{{#items}}{{#if loop.index1 == 1}}F{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_loop_size       "{{#items}}{{#if loop.size == 2}}S{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_loop_roots      "{{#items}}{{#if qty == limit}}Y{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_loop_value_rhs  "{{#items}}{{#if qty == count}}Y{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_loop_bool       "{{#items}}{{#if on == active}}Y{{else}}N{{/if}}{{/items}}")
    add_codegen_case(cmp_string_int      "{{#if numeric == age}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_invalid_string  "{{#if invalid_numeric == age}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_string_float    "{{#if numeric == ratio}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_int_string      "{{#if qty == alias}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_float_vars      "{{#if ratio >= ceiling}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_optional_rhs    "{{#if qty == opt_limit}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_optional_lhs    "{{#if opt_limit == qty}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_enum_literal    "{{#if level == 1}}Y{{else}}N{{/if}}")
    add_codegen_case(cmp_enum_var        "{{#if level == other_level}}Y{{else}}N{{/if}}")
    add_codegen_case(two_stage_nested_take "{{#tags}}{{#items | take(1)}}{{name}}{{/items}}{{/tags}}")
    add_codegen_case(two_stage_nested_index "{{#tags}}{{#items}}{{loop.index}}{{name}}{{/items}}{{/tags}}")
    add_codegen_case(if_loop_active      "{{#items}}{{#if active}}T{{else}}F{{/if}}{{/items}}")
    add_codegen_case(if_loop_not_active  "{{#items}}{{#if !active}}F{{else}}T{{/if}}{{/items}}")
    add_codegen_case(if_loop_and         "{{#items}}{{#if active && age}}A{{else}}B{{/if}}{{/items}}")
    add_codegen_case(if_loop_or          "{{#items}}{{#if active || age}}A{{else}}B{{/if}}{{/items}}")
    add_codegen_case(if_loop_filtered    "{{#items}}{{#if age | gt(18)}}A{{else}}B{{/if}}{{/items}}")
    add_codegen_case(cmp_bool_section_loop
      "{{#active}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/active}}")
    add_codegen_case(cmp_optional_section_loop
      "{{#opt}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/opt}}")
    add_codegen_case(cmp_number_section_loop
      "{{#age}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/age}}")
    add_codegen_case(cmp_inherited_bool_loop
      "{{#items}}{{#active}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/active}}{{/items}}")
    add_codegen_case(cmp_inherited_optional_loop
      "{{#items}}{{#opt_limit}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/opt_limit}}{{/items}}")
    # partial の通常ケースはスコープごとに個別名前空間へ生成し、
    # test_codegen_live_partials.cpp で VM と比較する。
    function(add_codegen_case_partial name tmpl)
      set(out ${CG_LIVE_DIR}/case_${name}.hpp)
      add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:injamm_codegen> -e "${tmpl}" -t CaseData -p case_${name} -n case_${name} -o ${out}
        DEPENDS injamm_codegen
        VERBATIM)
      set(CG_LIVE_HEADERS ${CG_LIVE_HEADERS} ${out} PARENT_SCOPE)
    endfunction()
    add_codegen_case_partial(partial_loop      "{{#partialdef g}}{{name}}{{/partialdef}}{{#items}}{{>g}}{{/items}}")
    add_codegen_case_partial(partial_root      "{{#partialdef h}}{{root.name}}-{{name}}{{/partialdef}}{{#items}}{{>h}}{{/items}}")
    add_codegen_case_partial(partial_toplevel  "{{#partialdef t}}{{name}}{{/partialdef}}{{>t}}")
    add_codegen_case_partial(partial_scope_refs
      "{{#partialdef s}}{{root_only}}|{{profile.name}}|{{profile_2E_name}}|{{root_only | upper}}|{{#if active}}T{{else}}F{{/if}}|{{#if age > 18}}G{{else}}S{{/if}}|{{#if active && age}}A{{else}}B{{/if}}|{{#items | take(1)}}S:{{name}}{{/items}}|{{#items}}I:{{loop.index}}{{/items}}{{/partialdef}}{{#items}}[{{>s}}]{{/items}}")
    add_codegen_case_partial(partial_compare_loop
      "{{#partialdef inner}}{{#if this == \"t1\"}}T{{else}}N{{/if}}:{{#if loop.index == 0}}F{{else}}L{{/if}}:{{#if loop.index1 == 1}}O{{else}}I{{/if}}:{{#if loop.size == 3}}S{{else}}X{{/if}}{{/partialdef}}{{#partialdef outer}}{{>inner}}{{/partialdef}}{{#tags}}[{{>outer}}]{{/tags}}|{{#items}}[{{>outer}}]{{/items}}")

    function(add_codegen_namespaced_case name tmpl ns prefix)
      set(out ${CG_LIVE_DIR}/case_${name}.hpp)
      add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:injamm_codegen> -e "${tmpl}" -t CaseData -p "${prefix}" -n "${ns}" -o ${out}
        DEPENDS injamm_codegen
        VERBATIM)
      set(CG_LIVE_HEADERS ${CG_LIVE_HEADERS} ${out} PARENT_SCOPE)
    endfunction()
    add_codegen_namespaced_case(ns_alpha "{{name}}" codegen_ns_alpha case_ns_alpha)
    add_codegen_namespaced_case(ns_beta  "{{name}}" codegen_ns_beta  case_ns_beta)
    add_codegen_namespaced_case(ns_guard_alpha "{{name}}" codegen_ns_guard_alpha case_ns_guard)
    add_codegen_namespaced_case(ns_guard_beta  "{{name}}" codegen_ns_guard_beta  case_ns_guard)
    add_codegen_namespaced_case(ns_case_lower "{{name}}" codegen_ns_case case_ns_case)
    add_codegen_namespaced_case(ns_case_upper "{{name}}" CODEGEN_NS_CASE case_ns_case)
    add_codegen_namespaced_case(ns_tuple_a "{{name}}" a_b c)
    add_codegen_namespaced_case(ns_tuple_b "{{name}}" a b_c)
    add_codegen_namespaced_case(partial_shared_alpha
      "{{#partialdef shared}}A:{{name}}{{/partialdef}}{{#partialdef alpha}}X:{{name}}{{/partialdef}}{{>shared}}"
      codegen_shared_partial codegen_partial_shared_alpha)
    add_codegen_namespaced_case(partial_shared_beta
      "{{#partialdef shared}}B:{{name}}{{/partialdef}}{{#partialdef beta}}Y:{{name}}{{/partialdef}}{{>shared}}"
      codegen_shared_partial codegen_partial_shared_beta)

    add_executable(test_codegen_live
      test_codegen/test_codegen_live.cpp
      test_codegen/test_codegen_live_partials.cpp
      test_codegen/test_codegen_live_namespaces.cpp
      ${CG_LIVE_HEADERS}
    )
    target_link_libraries(test_codegen_live PRIVATE injamm)
    target_include_directories(test_codegen_live PRIVATE include test_codegen ${CG_LIVE_DIR})
    target_compile_features(test_codegen_live PRIVATE cxx_std_23)
    add_test(NAME codegen_live_test COMMAND test_codegen_live)
  endif()

  install(
    TARGETS injamm_bc injamm_codegen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

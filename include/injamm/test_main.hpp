#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief テストランナー（レジストリベース）
 * @details グローバルレジストリを用いてテストケースを自動登録し、一括実行する機構を提供する。
 *          各テストケースは INJAMM_TEST マクロで定義し、登録されたテストは run_all() で逐次実行される。
 */
namespace test_runner {

/**
 * @brief 単一のテストケースを表す構造体
 * @details テスト名と実行可能な関数オブジェクトを保持する。
 *          registrar によって自動的に registry にプッシュされる。
 */
struct test_case {
  std::string name;            /**< テストケースの識別名 */
  std::function<void()> fn;    /**< テスト本体の処理 */
};

/**
 * @brief テストケースのグローバルレジストリを取得する
 * @details 関数ローカルの static 変数として std::vector<test_case> を保持する。
 *          複数の翻訳単位から安全にアクセスするため、ローカル static を使用している。
 * @return std::vector<test_case>& テストケースのリストへの参照
 */
inline auto& registry() {
  static std::vector<test_case> cases;
  return cases;
}

/**
 * @brief 登録されたすべてのテストを実行する
 * @details レジストリに登録されたテストケースを先頭から順に実行する。
 *          各テストは try/catch でラップされ、例外がスローされた場合は FAIL とみなす。
 * @return int 失敗したテストケースの総数
 */
inline int run_all() {
  int failed = 0;
  for (auto& tc : registry()) {
    try {
      tc.fn();
      std::cout << "  [PASS] " << tc.name << '\n';
    } catch (std::exception const& e) {
      std::cerr << "  [FAIL] " << tc.name << ": " << e.what() << '\n';
      ++failed;
    } catch (...) {
      std::cerr << "  [FAIL] " << tc.name << ": unknown exception\n";
      ++failed;
    }
  }
  return failed;
}

/**
 * @brief テストを自動登録する RAII ヘルパー
 * @details コンストラクタでレジストリにテストケースを追加する。
 *          静的初期化時にグローバルオブジェクトとして宣言することで、
 *          main() 実行前に自動登録が完了する。
 */
struct registrar {
  /**
   * @brief テストケースをレジストリに登録する
   * @param name テストケースの名前（std::string_view）
   * @param fn   テスト本体の処理（std::function<void()>）
   */
  registrar(std::string_view name, std::function<void()> fn) {
    registry().push_back({std::string{name}, std::move(fn)});
  }
};

} // namespace test_runner

// ---- マクロ ----

/**
 * @brief テスト関数を定義し、グローバルレジストリに登録する
 * @details 指定された name で static 関数 test_##name を定義し、
 *          同時に static な registrar オブジェクトでレジストリに登録する。
 *          関数本体は続くブロックで記述する。
 * @param name テストケース名（識別子として有効な文字列）
 */
#define INJAMM_TEST(name)                                             \
  static void test_##name();                                             \
  static test_runner::registrar reg_##name{#name, test_##name};         \
  static void test_##name()

/**
 * @brief 条件が真であることを表明する
 * @details 条件式 cond が false と評価された場合、ファイル名と行番号を含むエラーメッセージとともに
 *          std::runtime_error をスローする。
 * @param cond 評価する条件式
 */
#define EXPECT_TRUE(cond)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::ostringstream _oss;                                                 \
      _oss << "EXPECT_TRUE failed: " #cond " at " __FILE__ ":" << __LINE__;   \
      throw std::runtime_error(_oss.str());                                    \
    }                                                                          \
  } while (false)

/**
 * @brief 条件が偽であることを表明する
 * @details EXPECT_TRUE の否定版。cond が true の場合に失敗する。
 * @param cond 評価する条件式
 */
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

/**
 * @brief 2値が等しいことを表明する
 * @details lhs と rhs が等しくない場合、両方の値をエラーメッセージに含めて
 *          std::runtime_error をスローする。値の文字列表現には operator<< を使用する。
 * @param a 左辺値（lhs）
 * @param b 右辺値（rhs）
 */
#define EXPECT_EQ(a, b)                                                             \
  do {                                                                              \
    auto const& _a = (a);                                                           \
    auto const& _b = (b);                                                           \
    if (!(_a == _b)) {                                                              \
      std::ostringstream _oss;                                                      \
      _oss << "EXPECT_EQ failed: " #a " == " #b "\n  lhs: " << _a                 \
           << "\n  rhs: " << _b << "\n  at " __FILE__ ":" << __LINE__;            \
      throw std::runtime_error(_oss.str());                                         \
    }                                                                               \
  } while (false)

/**
 * @brief 指定の例外型が送出されることを表明する
 * @details expr を評価した際に exc_type 型の例外がスローされることを確認する。
 *          異なる型の例外や例外がスローされなかった場合は失敗とみなす。
 * @param expr     評価する式
 * @param exc_type 期待する例外型
 */
#define EXPECT_THROWS_AS(expr, exc_type)                                        \
  do {                                                                          \
    bool _threw = false;                                                        \
    try {                                                                       \
      (void)(expr);                                                             \
    } catch (exc_type const&) {                                                 \
      _threw = true;                                                            \
    } catch (...) {                                                             \
    }                                                                           \
    if (!_threw) {                                                              \
      throw std::runtime_error("EXPECT_THROWS_AS failed: " #expr               \
                               " should throw " #exc_type                      \
                               " at " __FILE__ ":" + std::to_string(__LINE__));\
    }                                                                           \
  } while (false)

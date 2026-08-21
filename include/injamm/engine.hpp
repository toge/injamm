#pragma once

/**
 * @file engine.hpp
 * @brief Bytecode VM 公開 API — engine / callback_sink / bind / make_partial
 * @details escape_hatch.hpp から分割。実行時コンパイル + VM レンダリングの
 *          公開 API のみを収容。NTTP レンダリングには依存しない。
 */

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <algorithm>

#include "types.hpp"
#include "bytecode.hpp"
#include "bytecode_compile.hpp"
#include "bytecode_exec.hpp"
#include "bytecode_debug.hpp"

namespace injamm {

/**
 * @brief コンテナを NTTP 名でバインドした bound_context を生成する
 *
 * @details 複数のコンテナを NTTP 文字列名と対応付けた bound_context を返す。
 *          戻り値の bound_context は参照を保持するため、元コンテナの生存期間内で使用すること。
 *          渡すコンテナ数と Names の数が一致しない場合はコンパイルエラーとなる。
 *
 * @tparam Names      バインドする変数名の NTTP fixed_string パック
 * @tparam Containers コンテナ型パック（推論）
 * @param  values     バインドするコンテナへの const 参照パック
 * @return detail::bound_context<detail::name_list<Names...>, Containers...>
 */
template <fixed_string... Names, typename... Containers>
[[nodiscard]] auto bind(Containers const&... values) -> detail::bound_context<detail::name_list<Names...>, Containers...> {
  static_assert(sizeof...(Names) == sizeof...(Containers), "injamm: bind() requires the same number of names and containers. "
                                                           "Example: bind<\"items\", \"user\">(items, user)");
  return detail::bound_context<detail::name_list<Names...>, Containers...>{std::forward_as_tuple(values...)};
}

template <typename T>
[[nodiscard]] auto bind(T const& value) -> detail::bound_context<detail::name_list<fixed_string{"_"}>, T> {
  return detail::bound_context<detail::name_list<fixed_string{"_"}>, T>{std::forward_as_tuple(value)};
}

/**
 * @brief 出力をコールバック関数にストリーミングする sink
 *
 * @details レンダリング結果を断片単位で受け取り、内部バッファに chunk_size バイト
 *          溜まるまで蓄積してからコールバック fn_(std::string_view) を呼ぶ。
 *          ファイル出力やネットワーク送信で出力全体を std::string に構築せずに
 *          書き出すために使う。render() 完了後は flush() で残りを排出する。
 *
 * @tparam Fn void(std::string_view) を呼び出せる関数オブジェクト型
 */
template <class Fn>
class callback_sink {
public:
  explicit callback_sink(Fn fn, std::size_t chunk_size = 4096) : fn_(std::move(fn)), chunk_size_(chunk_size) {}

  void append(std::string_view sv) {
    if (!buf_.empty() && buf_.size() + sv.size() > chunk_size_) flush();
    buf_.append(sv.data(), sv.size());
  }
  void append(char const* p, std::size_t n) { append(std::string_view{p, n}); }

  /** @brief 蓄積中の残り出力をコールバックに渡す */
  void flush() {
    if (!buf_.empty()) {
      fn_(std::string_view{buf_.data(), buf_.size()});
      buf_.clear();
    }
  }

private:
  Fn          fn_;
  std::size_t chunk_size_;
  std::string buf_;
};

/**
 * @brief バイトコード VM（実行時コンパイル）
 *
 * @details 実行時にテンプレート文字列をパースし、中間表現（Bytecode）に
 *          コンパイルしてからレンダリングを行う。
 *          ct_render に比べて柔軟性が高く、セクション / if / @変数 / ネストパス
 *          などの全機能をサポートする。
 *
 * @tparam T コンテキスト値の型（glz::meta<T> 要特殊化）
 */
template <class T>
class engine {
  detail::bytecode bc_;
  /** @brief 前回レンダリングの出力サイズ（次回 reserve のヒント。relaxed で十分） */
  mutable std::atomic<std::size_t> last_size_{0};

  public:
  engine() = delete;

  /** atomic メンバによる暗黙削除を避けるためコピー/ムーブを明示定義（ヒントは引き継ぐ） */
  engine(engine const& o) : bc_(o.bc_), last_size_(o.last_size_.load(std::memory_order_relaxed)) {}
  engine(engine&& o) noexcept : bc_(std::move(o.bc_)), last_size_(o.last_size_.load(std::memory_order_relaxed)) {}
  engine& operator=(engine const& o) {
    bc_ = o.bc_;
    last_size_.store(o.last_size_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }
  engine& operator=(engine&& o) noexcept {
    bc_ = std::move(o.bc_);
    last_size_.store(o.last_size_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }

  /**
   * @brief テンプレート文字列から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   * @param trim_blocks 閉じタグ後の改行を除去する（デフォルト false）
   * @param lstrip_blocks ブロックタグ前の空白を除去する（デフォルト false）
   */
  explicit engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  template <class ConstMap>
  explicit engine(std::string_view tmpl, ConstMap const& consts, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, consts, trim_blocks, lstrip_blocks)) {}

  /**
   * @brief テンプレート文字列と登録済み partial から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   * @param partials 外部から注入する名前付き partial のリスト（{{> name}} 用）
   * @param trim_blocks 閉じタグ後の改行を除去する（デフォルト false）
   * @param lstrip_blocks ブロックタグ前の空白を除去する（デフォルト false）
   */
  explicit engine(std::string_view tmpl, std::vector<detail::partial_entry> partials, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, std::move(partials), trim_blocks, lstrip_blocks)) {}

  /**
   * @brief プリコンパイル済みバイトコードから構築（デシリアライズ用）
   *
   * @details save_bytecode() で保存されたバイトコードを読み込んだ結果を直接渡す。
   *          コンパイル済みのバイトコードをそのまま利用するため、テンプレート文字列の
   *          パース/コンパイルを行わない。
   *
   * @param bc プリコンパイル済みバイトコード
   */
  explicit engine(detail::bytecode bc) : bc_(std::move(bc)) {}

  /**
   * @brief レンダリングを実行する
   *
   * @param value コンテキスト値の const 参照
   * @return expected<std::string> レンダリング結果、またはエラー
   */
  [[nodiscard]] expected<std::string> render(T const& value) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    /** 前回の実測出力サイズを reserve ヒントとして渡し、レンダリング中の再確保を防ぐ */
    auto r = detail::bc_execute(bc_, value, last_size_.load(std::memory_order_relaxed));
    if (r) {
      last_size_.store(r->size(), std::memory_order_relaxed);
    }
    return r;
  }

  /**
   * @brief 名前付き partial のみをレンダリングする
   *
   * @param value コンテキスト値の const 参照
   * @param partial_name レンダリングする partial の名前
   * @return expected<std::string> レンダリング結果、またはエラー
   */
  [[nodiscard]] expected<std::string> render(T const& value, std::string_view partial_name) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
    if (it == bc_.partial_entries.end()) {
      return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
    }
    return detail::bc_execute(*it->bc, value);
  }

  /**
   * @brief レンダリング結果を既存バッファに書き込む（バッファ再利用用）
   *
   * @param value コンテキスト値の const 参照
   * @param out 出力先バッファ（内容はクリアされる）
   * @return expected<void> 実行結果、またはエラー
   */
  [[nodiscard]] expected<void> render(T const& value, std::string& out) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    return detail::bc_execute_into(bc_, value, out);
  }

  /**
   * @brief レンダリング結果を任意の sink にストリーミング出力する
   *
   * @details 出力全体を std::string に構築せず、断片単位で sink に書き出す。
   *          ファイル出力やネットワーク送信時にメモリ確保を抑制できる。
   *          callback_sink<Fn> を渡すとコールバック関数で断片を受け取れる。
   *
   * @tparam Sink detail::output_sink を満たす型（append を持つ。std::string は除外）
   * @param value コンテキスト値の const 参照
   * @param sink  出力先 sink（render 完了後に flush() が必要な場合あり）
   * @return expected<void> 実行結果、またはエラー
   */
  template <class Sink>
    requires detail::output_sink<Sink> && (!std::same_as<std::remove_cvref_t<Sink>, std::string>)
  [[nodiscard]] expected<void> render(T const& value, Sink& sink) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    return detail::bc_execute_into_sink(bc_, value, sink);
  }

  /**
   * @brief 名前付き partial を任意の sink にストリーミング出力する
   *
   * @param value コンテキスト値の const 参照
   * @param partial_name レンダリングする partial の名前
   * @param sink  出力先 sink
   * @return expected<void> 実行結果、またはエラー
   */
  template <class Sink>
    requires detail::output_sink<Sink>
  [[nodiscard]] expected<void> render(T const& value, std::string_view partial_name, Sink& sink) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
    if (it == bc_.partial_entries.end()) {
      return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
    }
    return detail::bc_execute_into_sink(*it->bc, value, sink);
  }

  /**
   * @brief コンパイル済みバイトコードを可読な形式に逆アセンブルする
   *
   * @details デバッグと最適化のために、内部バイトコードを人間に読みやすい形式で
   *          出力する。命令列・リテラルテーブル・変数参照テーブルを含む。
   * @return std::string 逆アセンブル結果
   */
  [[nodiscard]] std::string disassemble() const { return bc_.disassemble(); }

  /**
   * @brief 内部バイトコードへの const 参照を取得する
   *
   * @details save_bytecode() に渡してシリアライズしたり、逆アセンブル結果を
   *          取得したりするために内部のバイトコードを公開する。
   *
   * @return detail::bytecode const& 内部バイトコードへの const 参照
   */
  [[nodiscard]] detail::bytecode const& get_bytecode() const { return bc_; }
};

/**
 * @brief 名前付き partial エントリを構築する（{{> name}} レジストリ用）
 *
 * @tparam T コンテキスト型（glz::meta<T> 要特殊化）
 * @param name partial 名
 * @param body partial 本文のテンプレート文字列
 * @return detail::partial_entry engine コンストラクタへ渡すエントリ
 */
template <class T>
[[nodiscard]] detail::partial_entry make_partial(std::string name, std::string_view body,
                                                 bool trim_blocks = false, bool lstrip_blocks = false) {
  return detail::partial_entry{std::move(name),
                                std::make_shared<detail::bytecode>(detail::bc_compile<T>(body, trim_blocks, lstrip_blocks))};
}

// CT 用 partial レジストリ（型パック）を公開 API として露出
using detail::ct_partials;

}  // namespace injamm

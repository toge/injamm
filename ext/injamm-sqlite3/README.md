# injamm-sqlite3

injamm のテンプレートエンジンをベースに、**sqlite3_stmt から直接 HTML をレンダリング** するためのヘッダオンリー C++23 ライブラリ。

## コンセプト

通常の injamm は glaze リフレクションを用いて C++ 構造体からテンプレートに値を渡す。  
injamm-sqlite3 は **中間の構造体を経由せず**、SQL の結果セットを直接テンプレートに流し込む。

```cpp
// 従来: SQL → struct → template
struct user { std::string name; std::string email; };
auto eng = injamm::engine<user>("{{name}} <{{email}}>");
eng.render(user{name, email});

// injamm-sqlite3: SQL → 直接レンダリング
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(
  "{{name}} <{{email}}>"
);
eng.render(sqlite3_row_view{stmt});
```

## 要件

- C++23 コンパイラ (GCC 14+ 推奨)
- injamm (同リポジトリ)
- libsqlite3 (システム標準、`/usr/include/sqlite3.h`)

## 使い方

### 単一行

```cpp
#include <injamm/sqlite3/engine.hpp>
#include <injamm/sqlite3/adapter.hpp>

sqlite3_stmt* stmt;
sqlite3_prepare_v2(db, "SELECT name, email FROM users WHERE id = ?", -1, &stmt, nullptr);
sqlite3_bind_int(stmt, 1, 42);
sqlite3_step(stmt);

auto row = injamm::sqlite3::sqlite3_row_view{stmt};
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(
  "{{name}} <{{email}}>"
);
auto result = eng.render(row);
// result → expected<string>  ("Alice <alice@example.com>")
```

### 複数行 ({{#.}})

```cpp
sqlite3_stmt* stmt;
sqlite3_prepare_v2(db, "SELECT name FROM users", -1, &stmt, nullptr);

auto rows = injamm::sqlite3::sqlite3_result{stmt};
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(
  "<ul>{{#.}}<li>{{name}}</li>{{/.}}</ul>"
);
auto html = eng.render(rows);
// → "<ul><li>Alice</li><li>Bob</li></ul>"
```

## 制限

| 機能 | 状態 | 理由 |
|---|---|---|
| `{{var}}` / `{{{var}}}` | ✅ | エスケープ / 生出力 |
| 文字列フィルタ (`upper`, `trim` 等) | ✅ | 値は文字列として扱われる |
| `{{#.}}...{{/.}}` (行ループ) | ✅ | 前方専用カーソル |
| `{{#if var}}` | ✅ | 非空文字列は真 |
| `{{^var}}` (反転セクション) | ✅ | 空文字列は偽 |
| `{{#if status == "Pending"}}` / `!=` | ✅ | 文字列リテラルとの等値 / 不等値比較 |
| `{{loop.index}}` / `{{loop.index1}}` | ✅ | カウンタ |
| `{{loop.is_first}}` | ✅ | `index == 0` |
| `{{loop.is_last}}` | ❌ | 前方カーソルでは判定不可 |
| `{{loop.size}}` | ❌ | 事前カウントなし |
| `{{#if age > 18}}` | ❌ | 値は文字列、数値比較不可 |
| 整数フィルタ (`hex`, `zerofill` 等) | ❌ | 文字列→整数変換なし |
| 入れ子パス `{{addr.city}}` | ❌ | 実行時型はリフレクション不可能 |

## 設計

詳細は `docs/superpowers/specs/2026-06-19-resultset-template-design.md` 参照。

### アーキテクチャ

```
injamm (上流、無修正)
  └── bytecode_exec.hpp  (2010行)

injamm-sqlite3
  ├── concept.hpp        runtime_field_accessible, forward_iterable
  ├── executor.hpp       bytecode_exec.hpp を fork + runtime dispatch 分岐
  ├── engine.hpp         runtime_engine<T>
  └── adapter.hpp        sqlite3_row_view, sqlite3_result
```

### 名前空間

- 全て `injamm::sqlite3` 以下
- Fork した executor は `injamm::sqlite3::detail` (ODR 衝突回避)

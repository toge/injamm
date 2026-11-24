/**
 * @file test_codegen_badbc.cpp
 * @brief injamm_codegen が壊れた .bc を拒否することを確認する回帰テスト
 *
 * @details 巨大な命令数や改竄された literal_total_size など、
 *          library loader では拒否されるが CLI の独立リーダで検証漏れがあると
 *          abort していた入力を作り、非ゼロ終了することを確認する。
 *          あわせて codegen の機能回帰（local partial の描画・スカラー セクションの
 *          if 生成・{{@key}} の明確な拒否）も検証する。
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
void put_u32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void put_u64(std::string& s, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void put_i32(std::string& s, std::int32_t v) {
  put_u32(s, static_cast<std::uint32_t>(v));
}
void put_string(std::string& s, std::string_view v) {
  put_u64(s, v.size());
  s.append(v);
}

/** @brief ヘッダ + 本体の先頭を組み立てる */
std::string make_header(std::uint64_t literal_total_size) {
  std::string data = "IJBC";
  put_u32(data, 6);  // version
  data.push_back(0); // is_simple
  put_u64(data, literal_total_size);
  return data;
}

/** @brief 比較命令1件だけを含む v6 バイトコード fixture を組み立てる */
std::string make_compare_fixture(std::uint8_t compare_kind, std::string_view rhs_text,
                                 std::uint64_t int_filter_count, std::uint8_t int_filter = 8,
                                 std::int32_t int_arg = 0) {
  std::string data = make_header(0);
  put_u64(data, 2);
  data.push_back(10); // emit_if_eq
  put_u32(data, 2);
  put_u32(data, 0);
  put_u32(data, 0);
  data.push_back(17); // emit_endif
  put_u32(data, 0);
  put_u32(data, 0);
  put_u32(data, 0);
  put_u64(data, 0);
  put_u64(data, 1);
  put_string(data, "age");
  data.push_back(0);
  data.push_back(0);
  data.push_back(static_cast<char>(compare_kind));
  put_string(data, rhs_text);
  data.push_back(0);
  data.push_back(0);
  data.push_back(0);
  put_u64(data, 0);
  put_u64(data, int_filter_count);
  for (std::uint64_t i = 0; i < int_filter_count; ++i) {
    data.push_back(static_cast<char>(int_filter));
    put_i32(data, int_arg);
  }
  put_u64(data, 0);
  data.push_back(0);
  put_u64(data, 0);
  return data;
}

int run_case(std::string const& cli, std::string const& name, std::string const& data) {
  std::string const path     = "codegen_bad_fixture.bc";
  std::string const out_path = "codegen_bad_fixture.hpp";
  {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  std::string const cmd = "\"" + cli + "\" -i " + path + " -t Bad -o " + out_path;
  int const         rc  = std::system(cmd.c_str());
  std::remove(path.c_str());
  std::remove(out_path.c_str());
  if (rc == 0) {
    std::cerr << "FAIL: malformed .bc accepted (" << name << ")\n";
    return 1;
  }
  return 0;
}

int run_ok_bytecode_case(std::string const& cli, std::string const& name, std::string const& data) {
  std::string const path = "codegen_valid_fixture.bc";
  std::string const out_path = "codegen_valid_fixture.hpp";
  {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  std::string const cmd = "\"" + cli + "\" -i " + path + " -t Valid -o " + out_path;
  int const rc = std::system(cmd.c_str());
  std::remove(path.c_str());
  if (rc != 0) {
    std::cerr << "FAIL: valid .bc rejected (" << name << ")\n";
    std::remove(out_path.c_str());
    return 1;
  }
  std::remove(out_path.c_str());
  return 0;
}

/** @brief {{@key}} を含むテンプレートが非ゼロ終了で拒否されることを確認する
 *  @details 旧実装は "_itemN.@key" というコンパイル不能なメンバアクセスを
 *           生成したまま exit 0 で終了していた。 */
int run_atkey_rejection(std::string const& cli) {
  std::string const out_path = "codegen_reject_fixture.hpp";
  std::string const cmd      = "\"" + cli + "\" -e \"{{#items}}{{@key}}{{/items}}\" -t Ok -o " + out_path + " 2>codegen_err2.txt";
  int const         rc       = std::system(cmd.c_str());
  std::remove("codegen_err2.txt");
  if (rc == 0) {
    std::cerr << "FAIL: {{@key}} template was accepted by codegen (compiles to broken header)\n";
    return 1;
  }
  std::remove(out_path.c_str());
  return 0;
}

/** @brief テンプレートを -e で codegen し、生成ヘッダに期待パターンが含まれるか検証する */
int run_ok_case(std::string const& cli, std::string const& expr, std::string const& name, std::string const& expect) {
  std::string const out_path = "codegen_ok_fixture.hpp";
  std::string const cmd      = "\"" + cli + "\" -e \"" + expr + "\" -t Ok -o " + out_path + " 2>codegen_err.txt";
  int const         rc       = std::system(cmd.c_str());
  std::remove("codegen_err.txt");
  if (rc != 0) {
    std::cerr << "FAIL: codegen failed for " << name << "\n";
    return 1;
  }
  std::ifstream f(out_path);
  std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::remove(out_path.c_str());
  if (content.find(expect) == std::string::npos) {
    std::cerr << "FAIL: expected pattern missing in generated code for " << name << "\n";
    return 1;
  }
  return 0;
}

/** @brief 生成コードに旧比較実装由来の壊れた式が残らないことを検証する */
int run_forbidden_case(std::string const& cli, std::string const& expr,
                       std::string const& name, std::string const& forbidden) {
  std::string const out_path = "codegen_forbidden_fixture.hpp";
  std::string const cmd      = "\"" + cli + "\" -e \"" + expr + "\" -t Ok -o " + out_path + " 2>codegen_err.txt";
  int const         rc       = std::system(cmd.c_str());
  std::remove("codegen_err.txt");
  if (rc != 0) {
    std::cerr << "FAIL: codegen failed for " << name << "\n";
    std::remove(out_path.c_str());
    return 1;
  }
  std::ifstream f(out_path);
  std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::remove(out_path.c_str());
  if (content.find(forbidden) != std::string::npos) {
    std::cerr << "FAIL: forbidden generated pattern remains for " << name << ": " << forbidden << "\n";
    return 1;
  }
  return 0;
}

/** @brief README 記載の長い CLI オプションが短縮形と同様に受理されることを確認する */
int run_long_options_case(std::string const& cli) {
  std::string const out_path = "codegen_long_options_fixture.hpp";
  std::string const cmd = "\"" + cli + "\" --expr \"{{name}}\" --type Ok --output " + out_path +
                          " --namespace long_generated --prefix long_render";
  int const rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "FAIL: documented long options were rejected\n";
    std::remove(out_path.c_str());
    return 1;
  }
  std::ifstream f(out_path);
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::remove(out_path.c_str());
  if (content.find("namespace long_generated") == std::string::npos ||
      content.find("long_render(const T& data)") == std::string::npos) {
    std::cerr << "FAIL: long option values were not reflected in generated code\n";
    return 1;
  }
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: test_codegen_badbc <injamm_codegen>\n";
    return 2;
  }
  std::string const cli = argv[1];
  int failures = 0;

  // 1. 命令数が巨大（旧実装は reserve で abort）
  {
    std::string data = make_header(0);
    put_u64(data, 0xFFFFFFFFFFFFFFFFull);  // instruction count
    failures += run_case(cli, "huge instruction count", data);
  }

  // 2. literal_total_size が実データと不一致（過大 reserve の誘発）
  {
    std::string data = make_header(1ull << 60);
    put_u64(data, 0);  // instructions
    put_u64(data, 0);  // literals
    put_u64(data, 0);  // var_refs
    put_u64(data, 0);  // partials
    failures += run_case(cli, "literal_total_size mismatch", data);
  }

  // 3. {{@key}}（マップ反復キー）を含むテンプレートは明確に拒否されること
  failures += run_atkey_rejection(cli);

  // 4. local partial が生成コードに含まれること（旧実装は無音で欠落していた）
  failures += run_ok_case(cli, "A{{#partialdef greet local}}Hi {{name}}{{/partialdef}}B{{>greet}}C",
                          "local partial rendered", "Hi ");

  // 5. スカラー セクションが if 文として生成されること（旧実装は range-for で壊れていた）
  failures += run_ok_case(cli, "{{#active}}on{{/active}}", "scalar section if",
                          "codegen_scalar_section_v");

  // 6. コンテナ セクションが range-for として生成されること
  //    （スカラー分岐だけを出してコンテナ側の for を落とし、何も描画しなくなった回帰の検出）
  failures += run_ok_case(cli, "{{#items}}{{name}}{{/items}}", "container section range-for",
                          "for (const auto& _v1 : data.items)");

  // 7. 同一スコープの兄弟セクションで本体ラムダ名が衝突しないこと
  failures += run_ok_case(cli, "{{#items}}{{name}}{{/items}}{{#name}}x{{/name}}", "sibling sections",
                          "_body2");

  // 8. README 記載の長いオプション名
  failures += run_long_options_case(cli);

  // 9. 比較 RHS・this の旧 0/メンバ化が生成されないこと
  failures += run_forbidden_case(cli, "{{#if qty == limit}}Y{{else}}N{{/if}}",
                                 "variable comparison rhs", "data.qty == 0");
  failures += run_forbidden_case(cli, "{{#if name == alias}}Y{{else}}N{{/if}}",
                                 "string variable comparison rhs", "data.name == 0");
  failures += run_forbidden_case(cli, "{{#tags}}{{#if this == \\\"t1\\\"}}Y{{else}}N{{/if}}{{/tags}}",
                                 "this comparison lhs", "_item1.this");

  // 10. 空文字列リテラルは有効で、引用符を保持した式として生成されること
  failures += run_ok_case(cli, "{{#if name == \\\"\\\"}}Y{{else}}N{{/if}}",
                          "empty string comparison literal",
                          "compare_operand_kind::string_literal, data.name, \"\"))");

  // 11. 比較 metadata の欠落・未知値はヘッダ生成前に拒否されること
  failures += run_case(cli, "int literal comparison without payload",
                       make_compare_fixture(1, "", 0));
  failures += run_case(cli, "variable comparison with empty rhs",
                       make_compare_fixture(3, "", 0));
  failures += run_case(cli, "unknown comparison rhs kind",
                       make_compare_fixture(255, "", 0));
  failures += run_case(cli, "comparison without rhs metadata",
                       make_compare_fixture(0, "", 0));
  failures += run_ok_bytecode_case(cli, "legacy integer comparison payload",
                                  make_compare_fixture(0, "", 1, 8, 3));

  if (failures == 0) {
    std::cout << "codegen bad .bc rejected\n";
    return 0;
  }
  std::cerr << "codegen bad .bc: " << failures << " failures\n";
  return 1;
}

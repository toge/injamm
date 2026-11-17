/**
 * @file test_codegen_badbc.cpp
 * @brief injamm_codegen が壊れた .bc を拒否することを確認する回帰テスト
 *
 * @details 巨大な命令数や改竄された literal_total_size など、
 *          library loader では拒否されるが CLI の独立リーダで検証漏れがあると
 *          abort していた入力を作り、非ゼロ終了することを確認する。
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void put_u32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void put_u64(std::string& s, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

/** @brief ヘッダ + 本体の先頭を組み立てる */
std::string make_header(std::uint64_t literal_total_size) {
  std::string data = "IJBC";
  put_u32(data, 6);  // version
  data.push_back(0); // is_simple
  put_u64(data, literal_total_size);
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

  if (failures == 0) {
    std::cout << "codegen bad .bc rejected\n";
    return 0;
  }
  std::cerr << "codegen bad .bc: " << failures << " failures\n";
  return 1;
}

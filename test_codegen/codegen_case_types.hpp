#pragma once
/** @file codegen_case_types.hpp
 *  @brief codegen ライブテスト（test_codegen_live）用の共通データ型
 *
 *  @details injamm_codegen が -t D として受け取り、生成コードが
 *           data.<field> の直接アクセスを行うため、フィールド名は固定。
 *           VM (injamm::engine) と生成コードの両方から使われる。
 */
#include <optional>
#include <string>
#include <vector>

struct CaseProfile {
  std::string name = "ITEM";
};

struct CaseItem {
  std::string name;
  bool        on = false;
  CaseProfile profile;
  std::string profile_2E_name = "FLAT";
};

struct CaseData {
  std::string                name  = "R";
  bool                       active = true;
  int                        age    = 30;
  std::optional<std::string> opt    = std::string{"x"};
  std::string                root_only = "root";
  CaseProfile                profile{"ROOT"};
  std::vector<CaseItem>      items  = {{"a", true}, {"b", false}};
};

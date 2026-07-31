#!/bin/sh
# examples/codegen_bench の render_*.hpp を現在の injamm_codegen で再生成する。
# codegen を改良したらこのスクリプトを実行してから bench_codegen.cpp を再実行し、
# 改善前後のスループットを比較する。
#
# 使い方: examples/codegen_bench/gen.sh [injamm_codegen へのパス]
#   パス省略時は build/injamm_codegen または build-ci/injamm_codegen を探す。
set -eu

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

CG=""
if [ $# -gt 0 ]; then
  CG="$1"
else
  for cand in "$ROOT/build/injamm_codegen" "$ROOT/build-ci/injamm_codegen"; do
    if [ -x "$cand" ]; then CG="$cand"; break; fi
  done
fi
if [ -z "$CG" ] || [ ! -x "$CG" ]; then
  echo "error: injamm_codegen binary not found. build it first or pass a path." >&2
  exit 1
fi

# codegen_helpers.hpp は test_codegen/ が管理する単一ソース。ベンチにはコピーを同期する。
cp "$ROOT/test_codegen/codegen_helpers.hpp" "$DIR/codegen_helpers.hpp"

# bench_codegen.cpp の T_A/T_B/T_C と同一のテンプレートで生成すること。
"$CG" -e '{{#items}}[{{name}}/{{quantity}}/{{price}}]
{{/items}}' -t Item -p render_a -o "$DIR/render_a.hpp"
"$CG" -e '{{#items}}{{name|upper}}{{/items}}' -t Item -p render_b -o "$DIR/render_b.hpp"
"$CG" -e '{{#items}}{{root.active}}{{/items}}' -t Item -p render_c -o "$DIR/render_c.hpp"

echo "regenerated render_{a,b,c}.hpp with $CG"

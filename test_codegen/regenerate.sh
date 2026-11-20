#!/bin/bash
# test_codegen/render*.hpp の再生成スクリプト
#
# 使い方: ./regenerate.sh [injamm_codegen のパス]
# 既定は ../build/injamm_codegen。各ヘッダは CMakeLists.txt の test_codegen /
# test_codegen_ext がインクルードするテンプレートから生成する。
# 引数は生成する関数名（= -p の値 = render*.hpp の render 直後の名前）。
set -euo pipefail
CODEGEN="${1:-$(dirname "$0")/../build/injamm_codegen}"
OUT="$(cd "$(dirname "$0")" && pwd)"

gen() { # gen <関数名> <template>
  "$CODEGEN" -e "$2" -t D -p "$1" -o "$OUT/$1.hpp"
}

# --- test_codegen.cpp 用 (TestData / SortData) ---
gen render1        'Hello {{name}}, age={{age}}'
gen render2        'Name: {{name|upper}}, Lower: {{name|lower}}'
gen render3        'Items:
{{#items}}
- {{name}} x{{quantity}}
{{/items}}'
gen render4        '{{#if active}}Active{{else}}Inactive{{/if}}'
gen render5        'Order #{{order_id}}:
{{#if total > 1000}}[VIP]{{/if}}
{{#items}}
  {{name}}: ${{price}}
{{/items}}
Total: ${{total}}'
gen render14       '{{name}}|{{active}}|{{age}}'
gen render15       '{{primary_item}}|{{& primary_item}}'
gen render16       '{{#items | reverse | take(100)}}{{name}};{{/items}}'
gen render17       '{{#items | take_last(100)}}{{name}};{{/items}}'
gen render18       '{{order_id | numify}}|{{order_id | zerofill(7)}}|{{email | truncate(8)}}|{{name | left(10)}}|{{total | format(".2f")}}|{{name | replace("i","X")}}'
gen render19       '{{#items | stride(3,1)}}{{name}};{{/items}}'
gen render20       '{{#items | reverse | stride(3,1)}}{{name}};{{/items}}'
gen render_join    '{{#sitems | join(", ")}}{{this}}{{/sitems}}'
gen render_round   '{{ total | round(2) }}'
gen render_round_noarg '{{ total | round }}'

# --- test_codegen_extended.cpp 用 (User / TestData2) ---
gen render_ext1    '{{#if age > 20}}adult{{else}}minor{{/if}}'
gen render_ext2    '{{#if age == 30}}match{{else}}no match{{/if}}'
gen render_ext3    '{{#if active}}yes{{else}}no{{/if}}'
gen render_ext4    '{{{name}}}'
gen render_ext5    '{{name|upper|lower}}'
gen render6        'Hello {{this}} World'
gen render7        '{{#nums}}[{{.}}]{{/nums}}'
gen render8        '{{#items}}{{root.name}}:{{name}}{{/items}}'
gen render9        '{{#if !active}}inactive{{/if}}'
gen render10       '{{name | title}}'
gen render11       '{{#items}}[{{#loop.is_first}}F:{{name}}{{/loop.is_first}}][{{^loop.is_first}}N:{{name}}{{/loop.is_first}}]{{/items}}'
gen render12       '{{#items}}[{{#loop.is_last}}L:{{name}}{{/loop.is_last}}]{{/items}}'
gen render13       '{{name|json}}'
gen render_logic_or   '{{#if active || age}}YES{{else}}NO{{/if}}'
gen render_logic_and  '{{#if active && age}}YES{{else}}NO{{/if}}'
gen render_if_filtered '{{#if age|is_neg}}YES{{else}}NO{{/if}}'
gen render_drift_hex  '{{age | hex}}'
gen render_drift_fcmp '{{score | gt(90)}}'
gen render_drift_fadd '{{score | add(1)}}'
gen render_drift_repl '{{name | replace("", "X")}}'
gen render_drift_pad  '{{name | pad(10, "")}}'
gen render_drift_sub1 '{{name | substr(-1, 3)}}'
gen render_drift_sub2 '{{name | substr(1, 0)}}'

echo "done"

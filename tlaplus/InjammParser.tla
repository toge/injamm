----------------------------- MODULE InjammParser -----------------------------
(***************************************************************************)
(* injamm テンプレートパーサ（セクション構造）のトークンレベルモデル          *)
(*                                                                           *)
(* 対象: include/injamm/ct_parse.hpp（NTTP コンパイル時パーサ）と             *)
(*       include/injamm/bytecode_compile.hpp（ランタイム VM コンパイラ）の    *)
(*       セクション開閉タグ処理。                                             *)
(*                                                                           *)
(* トークン対応:                                                             *)
(*   "OpenA"  = "{{#a}}"   "OpenB"  = "{{#b}}"   (通常セクション開始)        *)
(*   "InvA"   = "{{^a}}"   "InvB"   = "{{^b}}"   (逆セクション開始)          *)
(*   "IfOpen" = "{{#if x}}"  "IfClose"  = "{{/if}}"    (if セクション)       *)
(*   "LoopOpen" = "{{#loop.i}}" "LoopClose" = "{{/loop.i}}" (loop セクション) *)
(*   "CloseA" = "{{/a}}"   "CloseB" = "{{/b}}"   (閉じタグ)                  *)
(*   "Txt"    = テキスト / 変数 / コメント等（構造に無関係な要素の総称）      *)
(*                                                                           *)
(* 抽象化（スコープ外）: {{else}}, フィルタ, {{>partial}}, exists,           *)
(*   trim_blocks/lstrip_blocks, バイトオフセット位置, 空キー ({{#}})。        *)
(*                                                                           *)
(* CT モデル  : ct_parse.hpp find_section_close（スタック走査 + 閉じタグ名    *)
(*              検証）を継続スタックで線形化。領域は半開区間 [pc, end)。      *)
(*              - 本体内の閉じタグは最内側の開始タグと一致しなければ          *)
(*                syntax_error "mismatched closing tag"                      *)
(*              - 領域終端までに対応閉じタグがなければ unexpected_end         *)
(*              - トップレベルの余分な閉じタグは syntax_error "stray"         *)
(* VM モデル  : bytecode_compile.hpp compile_body_impl(expected_close) の     *)
(*              線形走査 + 閉じタグ名検証 (1277-1283 行)。領域境界なし。      *)
(*              - 取り違え → syntax_error、未閉鎖 → unexpected_end            *)
(*              - トップレベル単独閉じ → syntax_error "stray closing tag"     *)
(*                (compile_impl 1707-1711 行)                                *)
(* REF        : 上記 VM の意味論 = SYNTAX.md:253（「閉じタグ名の取り違えは    *)
(*              構文エラー」）を表す参照スタック走査。                        *)
(*                                                                           *)
(* 不変条件:                                                                 *)
(*   InvAgreement     : VM と CT は成功/エラーで一致する（VM/CT 一致性）      *)
(*   InvRefVM         : VM モデルは参照仕様と一致（モデル忠実性のサニティ）   *)
(*   InvCTIllFormed   : 参照仕様がエラーとする入力は CT もエラーにする        *)
(*                      （SYNTAX.md:253 の仕様。違反 = バグ）                 *)
(*   InvRefCT_NoFalse : 参照仕様が成功の入力で CT が誤エラーしない（サニティ）*)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANT MaxLen    \* 入力トークン列の最大長

VARIABLES tin, ct, vm   \* 入力トークン列、CT パーサ状態、VM コンパイラ状態

Tokens == {"Txt", "OpenA", "OpenB", "InvA", "InvB", "IfOpen", "IfClose", "LoopOpen", "LoopClose", "CloseA", "CloseB"}

\* 長さ 0..MaxLen の全トークン列（TLC が全列挙する）
Seqs == UNION { [1..k -> Tokens] : k \in 0..MaxLen }

LenOf(f) == Cardinality(DOMAIN f)

\* シーケンスの末尾要素を除く（スタックの pop: 末尾 = top）
ButLast(s) == [i \in 1..LenOf(s) - 1 |-> s[i]]

ErrVals == {"none", "syntax", "end"}

(*************************** トークン補助演算子 ****************************)

OpenTok(marker, key) ==
  IF marker = "#" THEN IF key = "a" THEN "OpenA" ELSE "OpenB"
                  ELSE IF key = "a" THEN "InvA"  ELSE "InvB"

CloseTok(key) == IF key = "a" THEN "CloseA" ELSE "CloseB"

\* 構造タグの閉じキー（C++ スキャナの正規化と同じ: {{#if x}} → "if"）
TokKey(t) ==
  CASE t = "OpenA"    -> "a"
    [] t = "InvA"     -> "a"
    [] t = "CloseA"   -> "a"
    [] t = "OpenB"    -> "b"
    [] t = "InvB"     -> "b"
    [] t = "CloseB"   -> "b"
    [] t = "IfOpen"   -> "if"
    [] t = "IfClose"  -> "if"
    [] t = "LoopOpen" -> "loop"
    [] t = "LoopClose"-> "loop"
    [] OTHER          -> ""

IsOpenTok(t) == t \in {"OpenA", "OpenB", "InvA", "InvB", "IfOpen", "LoopOpen"}
IsCloseTok(t) == t \in {"CloseA", "CloseB", "IfClose", "LoopClose"}

(****************************** 参照仕様 REF ******************************)
(* 素朴なスタック走査。VM の意味論: 閉じタグは最内側の開始タグと一致する      *)
(* こと、トップレベルの余分な閉じタグもエラー（stray closing tag）。          *)

RECURSIVE RefScan(_, _, _)
RECURSIVE RefClose(_, _, _, _)
RefScan(inp, i, stk) ==
  IF i > LenOf(inp) THEN
    IF stk = <<>> THEN "none" ELSE "end"    \* 未閉鎖の開始タグが残っている
  ELSE CASE inp[i] = "Txt" -> RefScan(inp, i + 1, stk)
       [] IsOpenTok(inp[i]) -> RefScan(inp, i + 1, Append(stk, TokKey(inp[i])))
       [] IsCloseTok(inp[i]) -> RefClose(inp, i, stk, TokKey(inp[i]))
       [] OTHER -> RefScan(inp, i + 1, stk)

RefClose(inp, i, stk, key) ==
  IF stk = <<>> THEN "syntax"   \* トップレベルの余分な閉じタグ → stray closing tag
  ELSE IF stk[LenOf(stk)] = key THEN RefScan(inp, i + 1, ButLast(stk))
       ELSE "syntax"                            \* 取り違え → 構文エラー

RefErr(inp) == RefScan(inp, 1, <<>>)

(****************************** CT モデル *********************************)
(* find_section_close（スタック走査）をトークンレベルで忠実に写像。再帰パース  *)
(* を継続スタック (after, pend) で線形化:                                    *)
(*   after = 本体終了後に再開するトークン位置                                 *)
(*   pend  = この領域の終端（囲みセクションの閉じタグ位置 or 入力終端+1）      *)

\* find_section_close 相当: from から end までスタック走査する。
\* 戻り値: [st |-> "found", pos |-> 閉じ位置] / [st |-> "none"] / [st |-> "syntax"]
RECURSIVE CTScan(_, _, _, _, _)
CTScan(inp, i, to, key, stk) ==
  IF i >= to THEN [st |-> "none", pos |-> 0]
  ELSE LET t == inp[i]
       IN CASE t = "Txt" -> CTScan(inp, i + 1, to, key, stk)
       [] IsOpenTok(t)   -> CTScan(inp, i + 1, to, key, <<TokKey(t)>> \o stk)
       [] IsCloseTok(t)  -> IF stk = <<>>
                            THEN IF TokKey(t) = key THEN [st |-> "found", pos |-> i]
                                 ELSE [st |-> "syntax", pos |-> i]
                            ELSE IF stk[LenOf(stk)] = TokKey(t)
                                 THEN CTScan(inp, i + 1, to, key, ButLast(stk))
                                 ELSE [st |-> "syntax", pos |-> i]
       [] OTHER          -> CTScan(inp, i + 1, to, key, stk)   \* "Txt"

CTDone(m) == m.pc >= m.end /\ m.cs = <<>>

CTPop(m) ==
  LET top == m.cs[LenOf(m.cs)]
  IN [m EXCEPT !.pc = top.after, !.end = top.pend, !.cs = ButLast(m.cs)]

\* セクション開始タグの処理（find_section_close 呼び出し相当）
CTOpen(inp, m, key) ==
  LET c == CTScan(inp, m.pc + 1, m.end, key, <<>>)
  IN CASE c.st = "found"
        \* 対応閉じタグあり: 本体 [pc+1, c.pos)、閉じた後 c.pos+1 から再開
     -> [m EXCEPT !.cs = Append(m.cs, [after |-> c.pos + 1, pend |-> m.end]),
                 !.pc = m.pc + 1, !.end = c.pos]
     [] c.st = "none"
        \* 未発見: unexpected_end（エラー設定 → 全領域終了。実装どおり）
     -> [m EXCEPT !.err = "end", !.done = TRUE]
     [] OTHER
        \* 取り違え: syntax_error（エラー設定 → 全領域終了。実装どおり）
     -> [m EXCEPT !.err = "syntax", !.done = TRUE]

CTStep(inp, m) ==
  IF m.pc >= m.end THEN
    IF m.cs = <<>> THEN m ELSE CTPop(m)
  ELSE CASE inp[m.pc] = "Txt" -> [m EXCEPT !.pc = m.pc + 1]
       [] IsOpenTok(inp[m.pc])  -> CTOpen(inp, m, TokKey(inp[m.pc]))
       [] IsCloseTok(inp[m.pc]) ->
            \* トップレベルの余分な閉じタグ → stray closing tag（実装どおり）
            [m EXCEPT !.err = "syntax", !.done = TRUE]
       [] OTHER -> [m EXCEPT !.pc = m.pc + 1]

(****************************** VM モデル *********************************)
(* bytecode_compile.hpp compile_body_impl(expected_close) 相当。              *)
(* スタックは期待される閉じタグ名の履歴。領域境界はなく入力全体を走査する。    *)

VMDone(m) == m.done

\* 閉じタグ処理（bytecode_compile.hpp 1277-1283 行 + compile_impl 1707-1711 行相当）
VMClose(m, key) ==
  IF m.cs = <<>> THEN [m EXCEPT !.err = "syntax", !.done = TRUE]   \* stray closing tag
  ELSE IF m.cs[LenOf(m.cs)] = key
       THEN [m EXCEPT !.cs = ButLast(m.cs), !.pc = m.pc + 1]
       ELSE [m EXCEPT !.err = "syntax", !.done = TRUE]   \* 取り違え → syntax_error

VMStep(inp, m) ==
  IF m.pc > LenOf(inp) THEN
    IF m.cs = <<>> THEN [m EXCEPT !.done = TRUE]
    ELSE [m EXCEPT !.err = "end", !.done = TRUE]     \* 未閉鎖 → unexpected_end
  ELSE CASE inp[m.pc] = "Txt" -> [m EXCEPT !.pc = m.pc + 1]
       [] IsOpenTok(inp[m.pc])  -> [m EXCEPT !.cs = Append(m.cs, TokKey(inp[m.pc])), !.pc = m.pc + 1]
       [] IsCloseTok(inp[m.pc]) -> VMClose(m, TokKey(inp[m.pc]))
       [] OTHER -> [m EXCEPT !.pc = m.pc + 1]

(*************************** 状態・遷移・不変条件 **************************)

Init ==
  /\ tin \in Seqs
  /\ ct  = [pc |-> 1, end |-> LenOf(tin) + 1, cs |-> <<>>, err |-> "none", done |-> FALSE]
  /\ vm  = [pc |-> 1, cs |-> <<>>, err |-> "none", done |-> FALSE]

Next ==
  /\ tin' = tin
  /\ ct' = IF CTDone(ct) THEN ct ELSE CTStep(tin, ct)
  /\ vm' = IF VMDone(vm) THEN vm ELSE VMStep(tin, vm)

BothDone == CTDone(ct) /\ vm.done

TypeOK ==
  /\ tin \in Seqs
  /\ ct.pc  \in 0..(MaxLen + 2)
  /\ ct.end \in 0..(MaxLen + 2)
  /\ ct.err \in ErrVals
  /\ vm.pc  \in 0..(MaxLen + 2)
  /\ vm.err \in ErrVals

\* VM と CT は成功/エラーの別で一致しなければならない
InvAgreement == ~BothDone \/ (ct.err = vm.err)

\* VM モデルの忠実性サニティ（参照仕様と一致するはず）
InvRefVM == ~BothDone \/ (RefErr(tin) = vm.err)

\* SYNTAX.md:253 — 参照仕様がエラーとする入力（取り違え・未閉鎖・stray）は
\* CT パーサもエラーにしなければならない
InvCTIllFormed == ~BothDone \/ (RefErr(tin) # "none" => ct.err # "none")

\* サニティ: 参照仕様が成功の入力で CT が誤ってエラーにしないこと
InvRefCT_NoFalse == ~BothDone \/ (RefErr(tin) = "none" => ct.err = "none")

=============================================================================

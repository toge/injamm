# Taste (Continuously Learned by [CommandCode][cmd])

[cmd]: https://commandcode.ai/

# Architecture
- Follow YAGNI principle: don't add new features when equivalent functionality already exists (e.g., don't add `loop.is_first` when `{{#if @first}}` already provides the same). Confidence: 0.85
- Prioritize inja-compatibility over unified syntax for loop variables: use `loop.index`/`loop.index1`/`loop.first`/`loop.last`/`loop.size`/`loop.key` rather than `@index`/`@index1`/`@first`/`@last`. Backward compatibility breaking is acceptable. Confidence: 0.75
- Minimize template parser/dispatcher branches: adding new template syntax increases maintenance cost and risks performance regression. Confidence: 0.75

# Performance
- Benchmark stability is important: when first measurements show >10% regression, re-run multiple times to rule out system load noise before drawing conclusions. Confidence: 0.85
- Maintain performance budget of ±10% from baseline for new features. Confidence: 0.80
- Avoid new opcode/dispatch table growth when possible: minimize template feature additions to keep dispatch table size stable (e.g., 57→58 opcode addition was acceptable but should be avoided if alternatives exist). Confidence: 0.70
- When adding new VM opcodes, prefer adding only what's strictly needed rather than introducing cascading support for related features. Confidence: 0.75
- Precompute hot-path predicates at parse/compile time: e.g., cache `key.find('.')` result as a `bool has_dot` field on `bc_var_ref` so the per-instruction `string_view::find` scan in `for_each_field` is eliminated. Same applies to any linear scan executed on every instruction dispatch. Confidence: 0.85

# Workflow
See [workflow/taste.md](workflow/taste.md)

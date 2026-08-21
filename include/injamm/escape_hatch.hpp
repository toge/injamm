#pragma once

/**
 * @file escape_hatch.hpp
 * @brief Umbrella header — NTTP + Bytecode VM 公開 API (後方互換)
 *
 * @details 旧 1493 行の単一ヘッダを責務ごとに分割:
 *          - detail/nttp_data.hpp : CT パース・@var 展開・partial 依存解析・bytecode キャッシュ (detail)
 *          - nttp_render.hpp      : NTTP 公開 API render / render_partial
 *          - engine.hpp           : Bytecode VM 公開 API engine / callback_sink / bind / make_partial
 *          本ファイルは後方互換のため 3 つを再エクスポートするだけに縮退。
 *          新規コードは必要なヘッダを直接 include 可能。
 */

#include "detail/nttp_data.hpp"
#include "nttp_render.hpp"
#include "engine.hpp"

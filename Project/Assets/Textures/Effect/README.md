# Assets/Textures/Effect — エフェクトマスク置き場

AI エフェクト生成（`/effectgen` スキル）が扱う**エフェクト専用テクスチャ（マスク）**の置き場。

## 規約

- **形＝テクスチャ / 色＝EffectDef パラメータ**。テクスチャは**白黒＋α のマスク**として作る（黒背景に白で形を描く）。
  実際の色味は EffectDef の `startColor` / `endColor` / `blendMode`（Add で発光、Normal で素直に）で付ける。
  → 「形」と「色」を分離することで、1 枚のマスクを色違いで使い回せる＆生成が安定する。
- ファイル名は内容を表す英小文字（例 `soft_circle.png`, `shockwave_ring.png`, `streak.png`）。

## クック（PNG → DDS）

- ここに置いた `*.png` は [`tools/Python/cook_assets.py`](../../../tools/Python/cook_assets.py) が
  **ディレクトリ構造をミラーして** `Resources/Textures/Effect/*.dds`（BC7）に変換する。
  サブフォルダ対応のためのスクリプト改修は不要（再帰ミラー済み）。
- EffectDef の `texturePath` に書くのは**クック後のランタイムパス** `Resources/Textures/Effect/<name>.dds`。
  AI が形を確認するために見るのは**ソース PNG** `Assets/Textures/Effect/<name>.png`。
  この「見る PNG ↔ 書く DDS」対応は [`tools/EffectGen/assets.catalog.json`](../../../tools/EffectGen/assets.catalog.json) が持つ。

## 注意

- クックは既定で **SRGB**（`BC7_UNORM_SRGB`）。純粋な線形マスクにしたい場合のみ
  `cook_assets.py` の `LINEAR_TEXTURE_HINTS` に `"Effect"` を足すか、`MaskTexture/` 配下に置く。
  ただし既存エフェクト（`circle` / `gradationLine`）は SRGB クックなので、見た目を合わせるなら SRGB のままが無難。
- `circle.dds` / `gradationLine.dds` は**エンジン共有の既定テクスチャ**（GPU パーティクル既定・EffectDef 既定値など）で
  `Resources/Textures/` 直下に据え置き。ここ（Effect/）には**移動しない**。新規マスクのみここに置く。

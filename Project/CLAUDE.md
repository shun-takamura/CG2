# Project — DirectX12 自作ゲームエンジン

このフォルダ配下で作業する際のルール。`CG2_0_1.sln` がエントリ。

## 構成

- `DirectXGame/main.cpp` — `WinMain`。`Game` を生成して `Run()`。
- `DirectXGame/GameEngine/` — エンジン汎用コード。ゲーム固有の処理を入れない。
  - `Core/` — DirectX12基盤、ウィンドウ、入力、リソース管理
  - `Graphics/` — 描画（Sprite / Object3D / Particle / Skybox / Light / Camera など）
  - `Math/` — ベクトル・行列・クォータニオン・補間
  - `Sound/` `Utility/` — その他汎用機能
- `DirectXGame/Game/` — ゲーム固有コード。`Actors/`、`Scene/`。
- `DirectXGame/ImGUIManager/` — ImGui統合。`USE_IMGUI` マクロでガード。
- `Assets/` `Resources/` — モデル・テクスチャ・サウンド等。
- `externals/` — 外部ライブラリ（DirectXTex / DXC / ImGui 等）。

## アーキテクチャの流儀

- `Framework`（汎用）→ `Game`（固有）の継承構造を維持する。
- マネージャー系は `Initialize` / `Update` / `Draw` / `Finalize` の流れを揃える。
- シーンは `BaseScene` を継承し、各マネージャーへのポインタは Setter で受け取る。シーン切替は `SceneManager` + `SceneFactory`。
- インスタンスを一つしか作る必要のないものはシングルトンを積極的に活用する（例: `Game::GetInstance()`）。
- 数学は `Math/` 配下の型（`Vector3`, `Matrix4x4`, `Quaternion`, `Transform` 等）を使う。`DirectXMath` 型を直に外へ漏らさない。

## コーディング規約

- クラス名は PascalCase、メンバ変数は末尾アンダースコア（例: `dxCore_`, `position_`）。
- DirectXリソースは `Microsoft::WRL::ComPtr` で持つ。`new`/`delete` は使わず、所有権は `std::unique_ptr`、参照のみは生ポインタ。
- ヘッダでは前方宣言を優先し、重い include（`d3d12.h` 等）は必要な場所だけにする。
- コメントは日本語。理由が自明なコメントは書かない。
- ImGui関連クラスは `USE_IMGUI` でガードし、Inspector対応するなら `IImGuiEditable` を継承。

## 新規ファイル作成のルール

1. **必ず事前に相談する。** 配置場所・命名・既存クラスへの追加で済まないかをユーザーに確認してから作る。
2. 適切な既存フォルダがなければ新規フォルダを作って配置する。
3. 作成したら `CG2_0_1.vcxproj` と `CG2_0_1.vcxproj.filters` に `<ClCompile>` / `<ClInclude>` の追加書き込みを行う。
4. 追加書き込みを行った旨をユーザーに必ず伝える（どのファイルにどのエントリを足したか）。

## パフォーマンスについて

- パフォーマンス向上が見込める手段（描画コール削減、定数バッファのまとめ、SRVのバッチ化、ヒープ確保の削減、SIMD、`std::move`、コンパイル時計算 など）に気付いたら**積極的にユーザーに提案する**。勝手に大規模な書き換えはせず、選択肢と想定効果・コストを提示してから進める。

- ## Claude Code のモデル戦略

- **計画フェーズ（設計・構成検討）** — Haiku を使用。設計方針、ファイル配置、既存コードへの影響を整理。
- **実装フェーズ（コード記述）** — Opus を使用。実装を進める際は、Haiku で立てた計画に従い Opus で実装。

同じチャット内で段階的に進め、不要なモデル切り替えは避ける。

## ビルド

- Visual Studio 2022 (`v143`) で `CG2_0_1.sln` を開く。`x64` プラットフォーム、構成は `Debug` / `Development` / `Release`。
- `USE_IMGUI` の有無で ImGui 関連がコンパイルされるかが変わる。

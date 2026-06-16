---
name: effectgen
description: 参考画像（GIF/PNG）とテキストプロンプトから、自作 DirectX12 エンジンの EffectDef(JSON) を生成して Resources/Json/Effects/ に書き出すスキル。起動中ゲームが即ホットリロードし、エフェクトエディタでプレビュー＆微調整できる。ユーザーが「このエフェクトを作って」「炎/衝撃波/オーラを生成」等と画像やイメージを示したときに使う。
---

# effectgen — AI エフェクト生成

参考画像＋プロンプトから `EffectDef` を組み、`Project/Resources/Json/Effects/<name>.json` に書き出す。
起動中ゲームの `SceneEditorWindow::RefreshEffectsIfChanged`（Effects フォルダの最終書込時刻監視）が即ホットリロードし、
エフェクトエディタ（Game.exe の ImGui）でプレビューできる。**着手前にこの SKILL 全体と下記データを読むこと。**

## 参照データ（このスキルの核）

- **権威スキーマ**: [`Project/tools/EffectGen/effectdef.schema.json`](../../../Project/tools/EffectGen/effectdef.schema.json)
  — 全フィールド・enum 値・既定値。元は `GameEngine/Graphics/Effect/EffectDef.cpp`。迷ったら cpp を確認。
- **テクスチャ目録**: [`Project/tools/EffectGen/assets.catalog.json`](../../../Project/tools/EffectGen/assets.catalog.json)
  — 使えるマスクの caption / tags / 「sourcePng ↔ runtimePath」対応。
- **few-shot 例（既存4エフェクト）**:
  [`Resources/Json/Effects/Hit_Small.json`](../../../Project/Resources/Json/Effects/Hit_Small.json)（被弾：閃光板＋衝撃リング＋火花＋点光源）/
  [`Aura.json`](../../../Project/Resources/Json/Effects/Aura.json)（オーラ：Cylinder×2 を UV スクロール＋loop）/
  [`Barrier.json`](../../../Project/Resources/Json/Effects/Barrier.json)（バリア：Ring 発生の周回パーティクル×3）/
  [`WingEffect.json`](../../../Project/Resources/Json/Effects/WingEffect.json)（翼：方向初速パーティクル×4）。

## 生成手順

1. **入力を理解する。** 画像があれば Read で見る（GIF/PNG は代表 1〜3 フレームのみ、送りすぎ厳禁）。
   「何のエフェクトか・色・動き・寿命・ループ有無・スケール感」を言語化する。
2. **構成を決める。** どのコンポーネントを何個使うか:
   - `primitives` … 形のある単発描画（閃光板/衝撃波 Ring/オーラ Cylinder/斬撃 Plane/レーザー Beam/稲妻 Lightning）。
   - `particles` … 火花・破片・煙・きらめき・渦（GPU パーティクル）。
   - `lights`    … 爆発/閃光の瞬間的な点光源（startIntensity→endIntensity でフラッシュ）。
   - `sounds`    … 効果音（登録済み name がある場合のみ）。
3. **テクスチャを選ぶ。** catalog の caption/tags で選び、`texturePath` に **runtimePath(DDS)** を書く。
   既存マスクで足りなければ「新規マスクが要る」とユーザーに告げる（→ 後述「新規マスクが必要なとき」）。
4. **色はパラメータで付ける**（マスクは α透過＝RGB白＋αで形）。`startColor`/`endColor`＋`blendMode` で乗算。色はテクスチャに焼かず最小限に。
5. **最小 JSON で書く。** Load は完全寛容（未指定=デフォルト）。**デフォルトと異なるフィールドだけ**書く。冗長な全フィールド出力はしない。
6. **書き出す。** `Project/Resources/Json/Effects/<name>.json`（`name` フィールドとファイル名を一致）。
7. **検証する。** 下記「検証」。最終確認はユーザーが起動中ゲームのエディタでプレビュー（ホットリロード）。

## 技法集（ノウハウ）

- **メリハリ＝暗いものを混ぜる（桜井メソッド・最重要）** → エフェクトは Add で明るくなりがちで、明るい背景だと埋もれる。**明るい中心(Add)＋暗い縁シルエット(Normal・暗色)を重ねる**とコントラストが出て映える。爆発・閃光は必ずこの「明＋暗」セットで考える。
- **マスクは α透過・Plane 貼りの完成形** → catalog の textures は全て Plane(板ポリ)にビルボードで貼る完成形マスク。Ring/Cylinder メッシュへ複雑テクスチャを展開貼りすると引き延ばされ破綻するので使わない（リング/筒/ビーム/稲妻の幾何そのものは meshType で出す）。
- **発光・炎・閃光・エネルギー** → `blendMode:2`(Add)。重ねるほど明るくなる。`endColor` の α を 0 にしてフェードアウト。
- **素直な不透明/煙のシルエット** → `blendMode:1`(Normal)。
- **衝撃波** → `meshType:3`(Ring) を `startScale`→`endScale` で急拡大、α フェード。`gradationLine` を貼ると尾が出る。
- **ギザギザの爆発・被弾の輪郭** → `gizagiza_00` / `aquillos_00` を **Plane に貼って**急拡大。明中心(circle 等を Add)＋暗縁でメリハリ（桜井メソッド）。
- **ガラス割れ・ヒビ** → `BrokenGlass_00`(網状) / `BrokenGlass_01`(放射) を Plane に。シールド破砕・地割れ・インパクト。
- **稲妻・電撃** → テクスチャ `Lightning_00/01/02` を Plane に貼る（バリエーション3種）／線そのものは `meshType:7`(Lightning) でも可。
- **煙・粉塵** → `smoke_00` を Plane に、`blendMode:1`(Normal) で暗めに。爆発の余韻・ディゾルブ消散。
- **きらめき・収集・回復** → `sparkles_00`(十字星) / `sparkles_01`(フレア) を Add。
- **羽根・上昇** → `Feathers_00/01`(舞う羽根) / `rise_00`(立ち昇る炎・気)。
- **オーラ・柱・渦** → `meshType:4`(Cylinder)、`uvAutoScroll:true`＋`uvScrollSpeed` で流れ、`loop:true`。Aura.json 参照。
- **斬撃・閃光板** → `meshType:0`(Plane)、`billboardMode:"Full"`、細長い `startScale`（例 [0.5,5,1]）。
- **火花・破片** → particles `velocityMode:2`(放射) or `1`(方向)、`colorMode:1` で色補間。`billboardMode:"None"`＋`rotateSpeed` でタンブル。
- **周回するエネルギー（バリア/魔法陣）** → particles `emitShape:1`(Ring)＋`orbitEnabled:true`。Barrier.json 参照。
- **瞬間フラッシュ** → light `startIntensity` 大→`endIntensity:0`、短い `lifetime`（0.1〜0.2）。
- **空間の歪み（陽炎・爆風・水中・衝撃波）** → primitive `useDistortion:true`＋`distortionTexturePath` に catalog の `distortionMaps`（既定は `rippleNormal` = DistorionNormalmapTexture）の runtimePath を書く。歪みマップは**白黒マスクではなく法線方向(RG)エンコード**で、`textures`(色マスク)とは別カテゴリ。新規生成は困難なので基本は既存プリセットを使い回す。
- **時間設計**: 各コンポーネントの `startTime`/`lifetime` をずらして「閃光→衝撃波→火花の余韻」のような段階を作る。`totalDuration` は全体の最長に合わせる。
- **角度は radian**（rotate / startAngle / endAngle / rotateSpeed）。全周 = 6.2831853。

## enum 早見表（schema より）

- `meshType`: 0=Plane 1=Box 2=Sphere 3=Ring 4=Cylinder 5=Helix 6=Beam 7=Lightning
- `blendMode`: 0=None 1=Normal 2=Add(既定) 3=Subtract 4=Multiply 5=Screen
- `billboardMode`(文字列): "None" / "Full" / "YAxis"
- particle `colorMode`: 0=Random 1=Fixed ／ `emitShape`: 0=Sphere 1=Ring ／ `velocityMode`: 0=全方向 1=方向固定 2=放射 3=接線
- `samplerMode`: 0=WrapAll 1=WrapU+ClampV 2=ClampAll ／ light `kind`(文字列): "Point" / "Spot"

## 新規マスクが必要なとき

catalog の既存マスクで形が出せない場合:
1. ユーザーに「この形のマスクが要る」と伝え、PNG を用意してもらう（または生成方針を相談）。
2. PNG は `Assets/Textures/Effect/<name>.png` に置く（**α透過: RGB全面白＋αチャンネルで形**。Plane に貼る完成形マスク）。
3. `python tools/Python/cook_assets.py` で `Resources/Textures/Effect/<name>.dds` にクック（ディレクトリ構造は自動ミラー）。
4. `assets.catalog.json` に sourcePng / runtimePath / caption / tags を追記。
5. EffectDef の `texturePath` に runtimePath を書く。
- ※ クックは既定 SRGB。`circle`/`gradationLine` 等エンジン共有テクスチャは**移動しない**（GPUパーティクル既定・EffectDef 既定値・DemoScene で参照されている）。
- **歪みマップ（distortionTexturePath 用）は白黒マスクとは別物**。catalog の `distortionMaps` から選ぶ（既定 `rippleNormal`）。法線方向 RG エンコードなので参考画像から生成するのは困難＝基本は既存プリセットを使い回す。本来は線形クックが正しいが既存は SRGB（catalog の `_distortionNote` 参照）。

## 検証

- **スキーマ整合**: enum 値・型・必須(`name`)を schema と突き合わせる。座標/色は配列長（vec2=2, vec3=3, color=4）。
- **パス実在**: `texturePath` の DDS が `Resources/Textures/...` に存在するか（新規マスクはクック済みか）。Glob で確認。
- **時間整合**: 各 `startTime + lifetime` が `totalDuration` に収まるか（loop でなければ）。
- **最終検証は実機**: 書き出し後、起動中ゲームのエフェクトエディタでホットリロードされプレビューできる。ユーザーに確認を促す。
- ※ JSON は自作パーサ（コメント許容）だが、Effects/*.json は素の JSON で書く（コメントなし）。
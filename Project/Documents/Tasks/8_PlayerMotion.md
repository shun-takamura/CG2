# 8. プレイヤーモーション

> **優先度 A** — 現状プレイヤーは **Mixamo の歩行サンプル（`Animated/Walk/walk.mesh`）そのまま**で、
> アニメーションは `walk.anim` **1本のみ**。射撃・回避・近接・被弾のモーションが**存在しない**。
> 近接4段コンボを実装しても、画面では棒立ちの人間が滑るだけ。
>
> 本ドキュメントは主に**必要なアニメーションの一覧（アセット制作の指示書）**。
> あわせて §4 に**スキニングの最適化**を含む（分身カウンターのアニメ再生がこれに阻まれているため）。

最終更新: 2026-11-27（§2 をまとまり A〜E の一覧に書き直し。作り方は 25 番）

---

## 1. 現状

> 2026-11-26 更新：自機は Tripo の天使の翼ロボット（`Resources/Models/Player/PlayerModel/`）に置き換わった。下の表は今の状態。

| 要素 | 状態 |
|---|---|
| プレイヤーモデル | `Resources/Models/Player/PlayerModel/`（体は Mixamo 名・Tポーズでボーン入れ済み） |
| アニメーション | スタート演出・HUB・死亡演出用の 9 本（Stand / Ignite / Catapult / Launch / Fly / Offer / OfferHold / Retract / Kneel）＋お試しの BossIdle。ゲーム中のクリップは §2 のまとまりごとに作る（25 番） |
| ゲーム中の姿勢 | `PlayerAnimPart`（`Resources/Json/Tuning/PlayerAnim/*.json`）：STG は Launch の最後のコマで止める、ボス戦は BossIdle |
| リターゲット | 未着手（Mixamo のクリップを新モデルへ流す作業） |
| 分身（ジャスト回避） | スキン無しの Object3D |
| クロスフェード | ✅ `PlayAnimation(animPath, fadeTime)`、再生速度は `SetPlaybackSpeed` |

### モデル待ちで保留中の項目

- 武器を手に持たせる（ボーンのワールド行列取得APIは実装済み）
- 武器ソケットからの発射 origin オフセット（trail エフェクトがキャラに食い込むのを防ぐ）
- プレイヤーモデルの origin 修正（現状**足元**。胸位置へ調整が必要）

---

## 2. 必要なアニメーション一覧

> 2026-11-27 書き直し：まとまり（A〜E）ごとに別々のチャットで作る。**作り方・ファイルの置き場所・JSON の書き方・衝突しないための決まりは 25 番**（各チャットは最初に 25 番を読む）。
> Mixamo には良いものが少ないので、**Claude がスクリプトでキーを作る**（既存の 9 本と同じ作り方）。
> 「状態」は `Resources/Json/Tuning/PlayerAnim/*.json` のキー（近接は `MeleeCombo.json`）。クリップを書くだけで鳴る。

### 2.1 A 近接（`player_clips/melee.py` → `MeleeCombo.json`）

| クリップ | ループ | 鳴らす所 | 備考 |
|---|---|---|---|
| B1〜B4 | — | `base.B[0..3].clip` | 4 段目はコンボの締めなので大きく |
| Y1〜Y4 | — | `base.Y[0..3].clip` | B より重い振り |
| 溜めの構え | ✅ | `charge.<種類>.holdClip` | 3 種類で共用可 |
| 溜めを放つ（単独／B4 段目から／Y4 段目から） | — | `charge.<種類>.levels[i].clip` | 段階違いは速度違い（`clipSpeed`）で流用可 |
| 特別なルートの技 | — | `routes.<押した順番>.clip` | 後で |

⚠️ 判定の出る時間（プレハブの `MeleeParams` の startup / activeDuration / recovery）に振りを合わせる（24 番）。

### 2.2 B 移動・回避（`player_clips/move.py` → `PlayerAnim/move.json`）

| クリップ | ループ | 状態 | 備考 |
|---|---|---|---|
| STG 立ち姿勢 | ✅ | `railStand` | 射撃中の姿勢。今は Launch の最後のコマで止めている |
| STG 滑空・前へ倒れた姿勢 | ✅ | `railGlide` | チャージ 1 段目が溜まった後、撃つと立ち姿勢へ（§2.2.1） |
| ボス戦の待機 | ✅ | `bossIdle` | 2026-11-27 お試しの `BossIdle`（軽く構えた立ち）あり。作り直してよい |
| 走り（前・後・左・右） | ✅ | `bossRunForward` / `Back` / `Left` / `Right` | 自機の向きから見た向き。最低でも前、無い向きは前で代用 |
| 回避（STG） | — | `railDodgeLeft` / `Right`（`Up` / `Down` / `Neutral`） | 最低でも左右 |
| 回避（地上） | — | `groundDodgeLeft` / `Right`（`Forward` / `Back` / `Neutral`） | 最低でも左右 |

#### 2.2.1 滑空状態（2026-10-09 決定）
参考はパルテナの鏡の「射撃していない時の滑空姿勢（正面からの当たり判定が小さくなる）」。
- **チャージ1段階目の完了と同時に滑空状態に入る**（`chargeStage1Triggered_`）。**射撃すると立ち姿勢へ戻る**。→ 2026-11-27 `StagePlayScene::SyncPlayerAnim` で `railGlide` へつないだ（クリップが入れば鳴る）。
- 滑空中は**正面からの当たり判定が小さくなる**（自機のカプセルは自機の回転に従う。前へ約 70° 倒すと正面から見た判定は「ほぼ半径 1 の円」）。→ 2026-11-27 `StagePlayScene::UpdatePlayerColliderPose` で、STG の**滑空中だけ**カプセルを `moveStateLeanDeg`（StagePlay.json、70°＝`RailGlide` の体の角度）倒すようにした（前は「攻撃していない間に 55°」。立ち姿勢 `RailStand` は起こしたので合わなくなった）。ビルド・実機未確認。
- 注意：回避・分身カウンター・必殺技で向きを固定している間（`fixedPlayerFacing_` 等）は滑空を解除する。

### 2.3 C 射撃（`player_clips/shoot.py` → `PlayerAnim/shoot.json`）

| クリップ | ループ | 状態 | 備考 |
|---|---|---|---|
| 通常の射撃 | — | `shoot` | `Shoot` / `ShootRail`（0.45 秒・構えで終わる。連射で鳴り直しても構えのまま） |
| チャージ中 | ✅ | `chargeHold` | `ChargeHold`。**地上だけ**（STG は滑空がチャージの見た目） |
| チャージを放つ | — | `chargeRelease` | `ChargeRelease` / `ChargeReleaseRail`（0.8 秒。腕は上げすぎず上体の反りで大きく） |
| 精密射撃の構え | ✅ | `precisionAim` | `PrecisionAim` / `PrecisionAimRail`。優先度 32＝射撃より上（撃ってもぶれない） |

2026-11-27 作成・書き出し・cook 済み（動きはユーザー了承：右肩を前に入れ、左腕は肩から力を抜いて垂らす）。STG 用（…Rail）は B の `RailStand`、地上用は `BossIdle` を元にし、`shoot.json` の `railClip` / `groundClip` で分ける。腕の向きは `call` で最後に付ける（STG の傾きの後でも銃が正面を向く）。**ビルド・実機未確認**（弾・ビームと銃口のずれ、盾のめり込みは実機で見る）。

### 2.4 D 被弾・回復・盾（`player_clips/defense.py` → `PlayerAnim/defense.json`）

| クリップ | ループ | 状態 | 備考 |
|---|---|---|---|
| 被弾（STG） | — | `railHit` | 短く。無敵中の点滅と重なる |
| 被弾（地上） | — | `groundHit` | ノックバックと重なる |
| 回復 | — | `heal` | |
| 盾を構える | ✅ | `shieldGuard` | 構える操作がまだ無い。呼ぶ所もこのまとまりで決める（25 番 §6） |

### 2.5 E 必殺技（`player_clips/special.py` → `PlayerAnim/special.json`）

| クリップ | ループ | 状態 | 備考 |
|---|---|---|---|
| Storm Lock（旧 傲慢サンダー）の発動 | ✅ | `stormLock` | 発動中ずっと（全画面ロック→一斉。無敵なので長めでよい） |
| Shatterline（旧 ディスラプター）の構え | ✅ | `shatterlineCharge` | |
| Shatterline の振り抜き | — | `shatterlineSlash` | 崩壊の間もこの状態 |
| Shatterline の硬直 | — | `shatterlineRecover` | 撃ち終わりの後隙 |

### 2.6 後で（`PlayerAnim/common.json`。持ち主はまだ無い）

| クリップ | 状態 | 備考 |
|---|---|---|
| クリア・勝利 | `victory` | ボスを倒した時（23 番の撃破の段取りと合わせる） |
| Landing の着地 | `landingTouchdown` | 着陸で足場に着いた時 |
| STG での死亡 | `railDeath` | 鳴らした後は死亡演出（PlayerDeathPart）に任せる。ボス戦の死亡は Kneel（作成済み） |

### 2.7 既存のクリップ（`player_clips/start.py`、作成済み）

Stand / Ignite / Catapult / Launch / Fly（スタート演出）、Offer / OfferHold / Retract（HUB の乗り込み）、Kneel（ボス戦の死亡）。

### 2.8 分身カウンター（派生4方向）

**新規クリップは不要。** 既存クリップの**序盤をスロー再生**して「これから何が起きるか」を見せる（上＝近接の溜めを放つ、右＝B1、下＝回避、左＝回復）。
⚠️ **これには分身4体をスキン付きで動かす必要があり、現状の性能では成立しない。** → §4

⚠️ **リグを揃えること。** `mixamorig:` のボーン名を前提にしている箇所がある（`player.json` の `weapon.bone = "mixamorig:RightHand"`）。

---

## 3. 注意点（アセット制作時）

### 3-1. モデルの origin を胸位置にする
現状足元原点。地上戦で攻撃が地面にめり込む問題があり、`StagePlay.json` の `"melee"` セクションに
オフセット回避のパラメータが入っている。**モデル側で直すのが本筋。**

### 3-2. `.mesh` / `.anim` / `.skel` / `.mat` の4分割形式
クックは `tools/Python/cook_assets.py`。形式は `asset_format_v2` を参照。

### 3-3. tangent が必要
`.mesh` は v3 で tangent(Vector4) を持つ。**法線マップを使うなら `--force` で再クック**すること。

### 3-4. アニメの法線マップ対応は済み
`Skinning.CS` が tangent を skeletonSpaceMatrix でスキニングするので、変形しても法線マップは正しく出る。

---

## 4. スキニング / アニメーションの最適化

> 最適化の候補と実測は **14番（`14_Optimization.md`）§4.5** にもまとめた。計測と実施の記録はそちらに書く。

> **結論：ゲーム完成後ではなく、ここでやる。**

### 4.1 なぜ今か

| | 理由 |
|---|---|
| **1** | これは性能の磨き込みではなく、**機能のブロッカー**。分身カウンターのアニメ再生（§2.4）が性能理由で実装できていない |
| **2** | **修正が局所的**。アーキテクチャ変更を伴わず、後回しにしても安くならない |
| **3** | 分身カウンター4方向は**実装上の最大の投資**。その見せ場が「半透明の棒立ち」のままなのは損失が大きい |
| **4** | 就活の観点でも「自作プロファイラで計測 → ホットパス特定 → 修正」は**語れる材料**になる（P.E.P.P.E.R. が既にある） |

### 4.2 観測されている事実

- player プレハブを複製した**スキン付き4体で fps が40台後半まで低下**（計測済み）
- そのため分身は**スキニング無しの Object3D**に変更して回避してある（現状）

### 4.3 原因の特定（コード読解による）

**GPU のスキニング処理そのものは原因ではない。** Mixamo 級のメッシュ5体分の頂点スキニングは
GPU にとって些少。犯人は **CPU 側のアニメーション適用**で、ホットスポットが2つある。

#### ホットスポット① 毎フレーム・毎ジョイントの `std::map<std::string, ...>` 検索

```cpp
// Skeleton.cpp:61-66
void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime) {
    for (Joint& joint : skeleton.joints) {
        if (auto it = animation.nodeAnimations.find(joint.name); ...)
```

`Animation::nodeAnimations` は **`std::map<std::string, NodeAnimation>`**（`Animation.h:43`）。
つまり**毎フレーム、全ジョイントについて文字列キーの赤黒木検索**をしている。

さらに `ApplyAnimationBlended`（`Skeleton.cpp:74-95`）は **1ジョイントあたり2回**引く（`:86` と `:95`）。
**クロスフェード中はコストが倍**になる。

> Mixamo リグは約65ジョイント。5体がブレンド中なら **5 × 65 × 2 = 650回/フレーム**の文字列検索。

#### ホットスポット② キーフレーム探索が毎回インデックス0からの線形走査

```cpp
// Animation.cpp:79-86
for (size_t index = 0; index < keyframes.size() - 1; ++index) {
    if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
```

**毎回先頭から線形に走査**している。アニメーションは通常**前へ進むだけ**なので、これは無駄。
1ジョイントにつき translate / rotate / scale の**3チャンネル**で走る。

> キー数60〜120のクリップなら平均30〜60反復 × 3チャンネル × 65ジョイント × 5体
> ≒ **数万〜十数万反復/フレーム**。ブレンド中は倍。

### 4.4 施策

| # | 施策 | 内容 | 効果 | コスト |
|---|---|---|---|---|
| **A** | **ジョイント→チャンネルの事前解決** | ロード時に `joint.name` → `NodeAnimation*` を一度だけ解決し、ジョイント順の配列に持つ。**毎フレームの文字列検索が消える** | 大 | 小 |
| **B** | **キーフレーム探索のカーソルキャッシュ** | チャンネルごとに「前回のインデックス」を保持し、そこから再開する。逆再生/シークは0へ戻してフォールバック | 大 | 小 |
| C | スキニング Dispatch のバッチ化 | 現在 `DispatchSkinning` が**インスタンスごとに バリア→Dispatch→バリア**（`AnimatedObject3DInstance.cpp:214-257`）。全インスタンスのバリアとDispatchをまとめれば直列化が解ける | 中 | 中 |

**A と B を先にやる。** どちらも局所的で、アーキテクチャに影響しない。
**C は A/B で足りなかった場合のみ。** GPU/CPU どちらが詰まっているかを先に確かめること。

### 4.5 進め方

```
1. PEPPER で現状を計測                  分身4体をスキン付きに戻した状態で
                                        ※self-time 計測は要ビルド
2. 施策 A を実装 → 再計測
3. 施策 B を実装 → 再計測
4. まだ足りなければ施策 C を検討
5. 分身をスキン付き（派生アニメの序盤スロー再生）へ戻す
```

**必ず 1 の計測を先にやること。** 上記は静的解析による推定であり、実測で裏を取る。
数値が残っていれば、そのまま就活の説明材料になる。

### 4.6 注意

- `ApplyAnimationBlended` は**2アニメ分**引くので、施策Aの効果はブレンド中に最も大きく出る。
  分身カウンターはまさにクロスフェードが走る場面なので、**狙いどころが一致している**
- 分身4体は**同じクリップの同じ時刻**を再生する場面が多い。将来的には**スケルトン計算の共有**も効くが、
  方向ごとに別クリップなので今回は対象外
- `skinningDispatchedThisFrame_` フラグ（`AnimatedObject3DInstance.cpp:207-210`）は
  シャドウパスとメイン描画の二重 Dispatch を防いでいる。**施策Cで触るときに壊さないこと**

---

## 5. 実装ステップ

```
アセット側
Step 1  リグを確定する                    mixamorig 系を維持（武器装着が依存）
Step 2  モデル本体を用意 / origin を胸へ
Step 3  Mixamo から取れるクリップを回収      待機/走り/回避/被弾/death/近接
Step 4  飛行姿勢2種を用意                  立ち・うつぶせ（設計の肝）
Step 5  必殺技2種を用意                    自作寄り
Step 6  プレハブの弾スロットへ割り当て        melee_w1..4 / melee_strong 等
Step 7  MeleeParams の尺をモーションに合わせる

エンジン側（§4）
Step 8   PEPPER で現状計測                  分身をスキン付きに戻した状態で
Step 9   施策A: ジョイント→チャンネル事前解決
Step 10  施策B: キーフレーム探索のカーソル化
Step 11  再計測 → 足りなければ施策C
Step 12  分身カウンターをスキン付きアニメへ戻す
```

**Step 8〜12 はアセット制作と並行できる**（既存の walk.anim だけで計測・検証が可能）。

---

## 6. 関連ファイル索引

| 役割 | パス |
|---|---|
| アニメ適用（ホットスポット①） | `DirectXGame/GameEngine/Graphics/Object3D/Skeleton.cpp:61-95` |
| キーフレーム補間（ホットスポット②） | `DirectXGame/GameEngine/Graphics/Object3D/Animation.cpp:69-110` |
| アニメ構造（`std::map<std::string,...>`） | `DirectXGame/GameEngine/Graphics/Object3D/Animation.h:40-44` |
| 毎フレーム更新 | `DirectXGame/GameEngine/Graphics/Object3D/AnimatedObject3DInstance.cpp:102-198` |
| スキニング Dispatch | `DirectXGame/GameEngine/Graphics/Object3D/AnimatedObject3DInstance.cpp:199-257` |
| Compute の設定 | `DirectXGame/GameEngine/Graphics/Object3D/SkinningComputeManager.cpp` |
| スキンクラスタ | `DirectXGame/GameEngine/Graphics/Object3D/SkinCluster.cpp` |
| 分身カウンター | `DirectXGame/Game/Scene/StagePlayScene.cpp`（`jdClonePath_[4]` / `UpdateJustDodgeClones`） |
| 近接パラメータ | `DirectXGame/Game/Components/MeleeParams.h` |
| プレイヤープレハブ | `Resources/Json/Prefabs/player.json`（`weapon.bone` / `bulletPrefabs`） |
| 現行モデル | `Resources/Models/Animated/Walk/`（walk.mesh / .anim / .skel / .mat） |
| アセットクック | `tools/Python/cook_assets.py` |
| プロファイラ | `P.E.P.P.E.R..md` / `tools/Python/analyze_profile.py` |

# 3. 敵の配置とバリエーション

> **優先度 S** — 「コアの遊びが成立しているか」を決める項目。
> 現状 **180秒のステージに敵8体**。これは「未完成」ではなく **まだゲームになっていない**水準。
> 仕組み（Waveシステム / コマンドパターン / インゲーム配置エディタ）は完成しているので、
> **不足しているのは配置作業と敵の種類**。

最終更新: 2026-09-16

---

## 0. このドキュメントの位置づけ

優先度3番目。`2_StageArt.md` §3「レール構造の制約」が**前提**なので先に読むこと。

このタスクは `stg_visual_overhaul` の **Phase E** にあたる。
計画上 **Phase E に入ったらレールの制御点の増減は禁止**（時刻が全部ずれるため）。

---

## 1. 現状サマリ

`Resources/Json/Waves/stage1.json` は **8エントリ**。内訳は Drone 6 / Carrier 1 / Rusher 1 の **3種類**。

180秒に対して **1分あたり2.7体**。

さらに、データを読むと3つの構造的な問題が出てくる。

| # | 問題 | 深刻度 |
|---|---|---|
| **1** | **トリガー秒が旧125秒尺の遺物**。全エントリが 6.25 の倍数（= 0.05 × 125秒）で、**新しい180秒尺に合わせて引き直されていない** | 高 |
| **2** | **93.75秒以降に敵が1体もいない**。180秒中 **後半86秒が完全な無人**。`Canyon` セクション（120秒〜）は丸ごと空 ⚠️ **ただしこれは配置作業の不足ではなく、谷の地形が未生成で置く場所が無いためのブロック**（→ §3.1） | **最高** |
| **3** | **敵スプラインが1本しかなく、しかも高高度に固定**。`EnemyPath_01` は Y≈881〜927。75秒・93.75秒のエントリはそこを参照しているが、その時刻のカメラは低空（Y≈40）まで降りている → **プレイヤーの約850m上空に湧く** | **最高** |

---

## 2. 現状詳細

### 2.1 stage1.json の全エントリ

| # | type | prefab | trigger | retreat | 配置方法 | 備考 |
|---|---|---|---|---|---|---|
| 0 | Drone | `drone` | 6.25s | 25.0s | `EnemyPath_01` | |
| 1 | Drone | `drone` | 12.5s | 31.25s | `EnemyPath_01` | |
| 2 | Drone | `drone` | 18.75s | 37.5s | `EnemyPath_01` | |
| 3 | Carrier | `carrier` | 43.75s | 68.75s | `EnemyPath_01` | 子= `drone`、間隔4s・上限3 |
| 4 | Rusher | `rusher` | 75.0s | -1 | `EnemyPath_01` | ⚠️ カメラは低空、スプラインは高高度 |
| 5 | Drone | `drone` | 93.75s | 112.5s | `EnemyPath_01` | ⚠️ 同上 |
| 6 | Drone | `HoverEnemy` | 10.75s | -1 | `cameraOffset` | ScreenHover。インゲームエディタで置いたもの |
| 7 | Drone | `drone` | 5.0s | -1 | `positions[]` | ワールド座標固定。`positions` 動作の実証 |

**トリガー秒**: 6.25 / 12.5 / 18.75 / 43.75 / 75 / 93.75 → すべて **6.25 の倍数**。
これは旧 `*_t` キー（正規化t）を **× 125秒** で自動換算した値がそのまま残っているため。
**現在のステージ尺は180秒**（`phase.seekMaxSec = 180`、`camera.speed = 1/180`）。

**空白時間**: 最後のトリガーが 93.75秒、最後の退避が 112.5秒。→ **112.5秒〜180秒（67.5秒間）は敵ゼロ。**

### 2.2 敵の見た目

| prefab | 実体 | HP | hurt / death エフェクト |
|---|---|---|---|
| `drone` | **Primitive Sphere・青** (`0.2, 0.5, 1.0`) | 30 | `Hit_Small` / `Death_Drone` |
| `carrier` | **Primitive Sphere・紫** (`0.6, 0.3, 0.8`) | 500 | `Hit_Small` / `Death_Drone` |
| `rusher` | **Primitive Plane・橙** (`1.0, 0.3, 0.1`) | 80 | ❌ **なし** |
| `turret` | `Resources/Models/Enemy/enemy.mesh` | 30 | ❌ なし |
| `HoverEnemy` | `Resources/Models/Enemy/enemy.mesh` | — | ❌ なし |
| `boss` | **Primitive Sphere・赤** (`0.85, 0.2, 0.25`) | 1500 | ❌ 空文字 |

**雑魚もボスも、実体は色付きのプリミティブ球。** 敵モデルは `enemy.mesh` の1個しかなく、それを使っているのは `turret` と `HoverEnemy` の2プレハブだけ。

### 2.3 ✅ できている仕組み（作業だけで物量を増やせる）

| 機能 | 実体 |
|---|---|
| Wave定義・IO | `Game/Wave/WaveDef.h` / `WaveDefIO`。キーは `trigger_sec` / `retreat_sec` / `traverse_sec` / `shoot_interval_sec`（**旧 `*_t` は ×125 で自動換算して読める**） |
| コマンドパターン | `IEnemyCommand` / `EnemyController` / `EnemyCommandFactory` |
| 実装済みコマンド 7種 | `ShootAtPlayerCommand` / `RetreatCommand` / `ChargeRushCommand` / `SpawnDroneCommand` / `WanderInScreenCommand` / `HoverStationCommand` / `BossAttackCommand` |
| 移動タイプ（プレハブ駆動） | `MovementType{SplineFollow, ScreenHover, Drift, Static}`。**プレハブの `movement` が enemyType より優先** |
| 攻撃ロール（Wave駆動） | `enemyType` = `Drone` / `Carrier` / `Rusher` |
| **ワールド座標固定配置** | `WaveEntry::positions[]` — **実装済み・entry7 で実使用中** |
| **画面相対配置** | `WaveEntry::cameraOffset` + `useCameraOffset`。ScreenHover が毎フレーム追従 |
| **インゲーム Wave エディタ** | StagePlay Tuning ▸ Wave Editor（Debug限定）。タイムラインをスクラブ → ビューポートにプレハブD&D → その瞬間のカメラから `cameraOffset` を逆算してエントリ生成 |
| Blender からの砲台配置 | エンプティに `is_turret` + trigger/retreat/shoot → Wave JSON へマージ（スプライン敵・ホバー敵は温存） |
| 決定論的 Seek | `spawnFired_` / `retreatFired_` / `killAtT_` で任意時刻へ巻き戻し |
| 敵同士の押し合い | `ApplyEnemyRepulsion` |

### 2.4 ❌ 設計にあるが未実装の敵

`game_design` で定義済みだが、まだ存在しない：

| 敵種 | 仕様 | 実装タイミング |
|---|---|---|
| **バリア持ち** | バリアHP250・本体150。通常弾は0ダメージ、チャージ弾/必殺技のみ有効。バリア展開/解除はディゾルブ、破壊時ガラス破片 | チャージ射撃の弾プレハブ作成と同時 |
| **爆発物型** | 1発撃破。ゆっくり接近し、撃破or接触で球範囲爆発＋**範囲内の敵を連鎖撃破** | ステージバリエーション前 |

また `MovementType::Drift` は enum にあるが、**`EnemyCommandFactory` に分岐が無く SplineFollow と同じフォールバックに落ちる**（実質未実装）。

---

## 3. 目標

### 3.1 ⚠️ Canyon は「地形待ち」でブロックされている

後半が無人なのは**配置作業をサボっているからではない。**
`Canyon` の敵は**谷の構造物（岩柱など）に固定砲台式で設置する**方針だが、**その谷の地形自体がまだ存在しない**（シーンのオブジェクトは試験用平板1枚のみ。→ `2_StageArt.md` §1）。

```
2_StageArt Phase C（谷の壁・岩柱の生成）  ← Step 4 へ繰り上げ済み
        │
        ├──> ここ（Canyon への固定砲台設置）
        └──> 4_StageGimmick（破壊可能な岩柱）
```

**したがって本タスクの Step 4 は、`2_StageArt.md` Step 4 の完了待ち。**
それ以外の Step（特に Step 2・3 の時刻とスプラインの引き直し）は**今すぐ着手できる**ので、先に片付けること。

### 3.2 数値目標

| 指標 | 現状 | 目標 |
|---|---|---|
| 総エントリ数 | 8 | **60〜100体相当** |
| 敵の種類 | 3（+ Static/Hover の配置バリエーション） | **6〜7**（バリア持ち・爆発物型を追加） |
| 敵のいない時間 | 67.5秒（後半まるごと） | **連続10秒以上の空白を作らない** |
| 敵スプライン | 1本 | セクションごとに複数 |
| 見た目 | プリミティブ球 | 最低限モデル化（→ 後述） |

### 密度の目安

3分＝3セクション構成に対して：

| セクション | 秒 | 狙い | 体数目安 |
|---|---|---|---|
| SkyAboveClouds | 0–54 | 導入。操作を覚えさせる | 15〜20 |
| CloudLayer | 54–66 | **視界が潰れる区間**。敵は少なく、通過の緊張に集中 | 2〜4 |
| LowAltitude | 66–120 | 本番。密度最大 | 25〜35 |
| Canyon | 120–180 | 終盤の山場 → ボスへ | 20〜30 |

---

## 4. 作業方針

### 4.1 配置手段の住み分け（確定済み）

| 敵の性質 | 配置手段 | 理由 |
|---|---|---|
| **ワールド基準**（固定砲台・地上設置物） | **Blender** | レベルアートと同じ場所で置ける |
| **画面相対**（ScreenHover） | **インゲーム Wave エディタ** | 「画面のどこに出すか」はその瞬間の画面を見ながら決めるのが最適 |
| スプライン追従 | インゲーム（スプラインはBlenderでも可） | |

**この住み分けは決定済みで、覆さない。** Blender のカメラをレール位置へ置く改造も、Python 側で world→cameraOffset を逆算するのも**やらない**（カメラの向きは回転キー補間で決まり Blender/Python 側に無いため、複製すると drift する）。

### 4.2 時刻の引き直し

旧125秒尺の値をそのまま180秒へ伸ばすのではなく、**セクション境界（0 / 54 / 66 / 120）を基準に置き直す**。

`2_StageArt.md` §3 の通り、レールの制御点は **3秒/点 × 60点**。
→ **1制御点 = 3秒** なので、「idx N の直前で出す」という感覚で時刻を決められる。

### 4.3 敵スプラインを増やす

現状 `EnemyPath_01`（5点・Y≈881〜927）の1本だけ。
**セクションごとに最低1本**、できれば「横切る」「正面から来る」「追い越す」で別々に用意する。

⚠️ レールの高度が **Y=900 → 40 → -60** と大きく変わるので、**スプラインは必ずその時刻のレール高度に合わせる**こと。現在の entry 4/5 はこれを外している。

---

## 5. 落とし穴（実装前に必読）

### 5-1. レールの制御点を増減させない
`SplineCurveActor::Sample` は弧長でなく**インデックス等分**（`SplineCurveActor.cpp:55`）。
制御点を1個挿入するだけで**以降の `trigger_sec` が全部ずれる**。
→ **Phase E（このタスク）に入ったら制御点の増減は厳禁。** 詳細は `2_StageArt.md` §3。

### 5-2. per-entry フラグの粒度
`spawnFired_` / `retreatFired_` / `killAtT_` は **WaveEntry インデックス単位の配列**。
1エントリが `positions[]` や `count` で複数体を出す設計にすると、撃破記録・退避・Seek再構築の粒度と噛み合わない。

安全策は2つ：
- **(a)** positions の各要素につき1体出すが、**per-entry フラグはエントリ単位のまま**（全部まとめて trigger/retreat）
- **(b)** Blender 側で 1体1エントリに展開して吐く

→ **(a) が `count` と同じ扱いになるので素直。**

### 5-3. ScreenHover はワールド座標で代替できない
`HoverStationCommand.h:41` が**毎フレーム** `target = CameraLocalToWorld(cam, ctx.hoverOffset)` で生きたカメラに追従する。
「カメラが進んでもプレイヤーから見て静止して見える」のが仕様なので、**スポーン時に一度ワールド座標へ焼く方式では実現できない**。`cameraOffset` は必須。

供給経路: `RailStagePart.cpp:231` → `GameScene.cpp:369` → `HoverStationCommand`

### 5-4. Static はスポーン時に一度だけワールドへ焼かれる
ScreenHover と違い、`Static` / `positions[]` は**カメラに追従しない**。
`CameraOffsetToWorld()` はオーサリング上の便法にすぎない。

### 5-5. `MovementType::Drift` は未実装
enum にはあるが `EnemyCommandFactory` に分岐が無く、SplineFollow と同じフォールバックに落ちる。
使うなら分岐を足すこと（`WanderInScreenCommand` が流用できる）。

### 5-6. Seek は Hover の経過時間を厳密復元しない
Seek は開発ツール。hover は停止状態から再開になる。`killAtT_` は内部的に進行度 t のまま（非ユーザー向け）。

### 5-7. Wave構造を変更したら `RebuildWaveRuntimeState()`
エントリの追加/削除/並替のあとは `Seek(秒)` で全再構築する（Seek が敵/弾/コントローラを全clear→再spawn）。

### 5-8. Blender の Export はシーンJSONを全置換する
ただし**敵は Wave JSON の別系統**なので構造的に巻き込まれない。砲台は `positions` 構造で「Blender管轄」を識別してマージしている（スプライン敵・ホバー敵は温存）。

### 5-9. 射撃間隔は実時間、スポーン/退避はレール進行同期
`shoot_interval_sec` はリアル秒基準（カメラ速度変更に非依存）だが、スポーン/退避はレール進行に同期する（**カメラが止まれば出現も止まる**）。

---

## 6. 実装ステップ

```
Step 1  レール制御点の凍結を確認          2_StageArt.md §3。以後増減しない
Step 2  敵スプラインをセクションごとに追加   高度をレールに合わせる（現状の最大のバグ）
Step 3  既存8エントリの時刻を引き直す       旧125秒尺の遺物を180秒尺へ
Step 4  Canyon セクション（120-180s）を埋める  ★2_StageArt Step 4（谷の地形）の完了待ち
        └ 岩柱などの構造物に固定砲台式で設置する。4_StageGimmick と同時に進める
Step 5  Drone/Rusher/Carrier で density を上げる  60〜100体相当へ
Step 6  敵モデルの割り当て                 プリミティブ球からの脱却（最低限 enemy.mesh を流用）
Step 7  hurt/death エフェクトの欠落を埋める   rusher / turret / HoverEnemy / boss
Step 8  バリア持ちを実装                   チャージ弾プレハブ作成と同時
Step 9  爆発物型を実装                     連鎖撃破が気持ちいいので動画映えする
Step 10 難易度カーブの調整                 プレイテストして詰める
```

**Step 2 と Step 3 は最優先。** 現状「見えない場所に湧いて、後半は何も出ない」ので、密度を上げる前にここを直さないと調整が意味を持たない。

**Step 6/7 は `6_PlayerMotion.md` および `1_Sound.md` Step 7 と同時に進めると効率が良い**（どれも「プレハブにアセットを割り当てる」作業）。

---

## 7. 関連ファイル索引

| 役割 | パス |
|---|---|
| Wave定義 | `DirectXGame/Game/Wave/WaveDef.h` / `WaveDef.cpp`（load L67-73 / save L104-109、配列キー `spawn_entries`） |
| コマンド生成 | `DirectXGame/Game/Enemy/EnemyCommandFactory.cpp` |
| コマンド群 | `DirectXGame/Game/Enemy/Commands/`（7ファイル） |
| ボスAI | `DirectXGame/Game/Enemy/Boss/`（`BossActionManager` / `BossStateMachine` / `Action/` 4種） |
| 移動パラメータ | `DirectXGame/Game/Components/MovementParams.h` |
| スポーン処理・Wave Editor | `DirectXGame/Game/Scene/RailStagePart.cpp`（`:181-` スポーン、`:186-199` cameraOffset、`:231` hoverOffset供給、`:465-495` `OnViewportPrefabDrop`） |
| ホバー追従 | `DirectXGame/Game/Enemy/Commands/HoverStationCommand.h:41` |
| コントローラ更新 | `DirectXGame/Game/Scene/GameScene.cpp:369` |
| Wave データ | `Resources/Json/Waves/stage1.json` |
| 敵プレハブ | `Resources/Json/Prefabs/`（`drone` / `carrier` / `rusher` / `turret` / `HoverEnemy` / `boss` / `EnemyBullet`） |
| 敵モデル（唯一） | `Resources/Models/Enemy/enemy.mesh` |
| 敵スプライン | `Resources/Json/Scenes/StagePlay.json` の `EnemyPath_01` |
| 配置ガイド | `Documents/EnemySpawnGuide.md` |

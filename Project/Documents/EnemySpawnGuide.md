# 敵配置・スプライン作成ガイド

レールシューティング道中の敵スポーンとスプライン経路の作り方をまとめる。

---

## 1. 全体の流れ

```
[シーン]   EnemyPath スプライン（経路）を配置・命名
    ↓
[JSON]    Resources/Json/Waves/stage1.json に spawn_entry を書く
    ↓
[実行]    StagePlayScene が読み込み、t トリガーで敵を出す
```

スポーンのトリガーは **カメラ進行度 t（正規化 0.0〜1.0）**。`RailCameraController::progress_` と同じ値。

---

## 2. スプラインを新規作成する

### 2.1 シーンエディタで追加

1. ゲームを `Debug` ビルドで起動
2. `SceneEditor` ウィンドウを開く
3. 上部の **Add Spline** で **`EnemyPath`** ボタンを押す
   - PlayerRail / FloatingPath / CameraPath は別用途。敵経路は **EnemyPath** を選ぶ
4. シーン中央にデフォルト位置で `Spline` という名前のスプラインが生成される

### 2.2 名前を変える

1. `Hierarchy` から生成したスプラインを選択
2. `Inspector` の `Name` フィールドを書き換える（例: `EnemyPath_02`）
3. **シーン内でユニークな名前にする** — `spawn_entries` の `spline_id` がこの名前を参照する

### 2.3 制御点を編集

`Inspector` の Spline 編集 UI で:
- 制御点の追加・削除
- 各点の座標編集
- ギズモでドラッグして配置

最低 **2 点必要**（Catmull-Rom の仕様）。3〜5 点くらいで滑らかな曲線になる。

### 2.4 シーン保存

`SceneEditor` の **Save** ボタンで現在のシーンを JSON 保存。スプラインも一緒に保存される。
保存先は `Resources/Json/Scenes/<scene_name>.json`。

---

## 3. 敵を配置する

### 3.1 JSON の場所

```
Project/Resources/Json/Waves/stage1.json
```

ステージごとに 1 ファイル。`spawn_entries` 配列に敵を並べる。

### 3.2 共通フィールド

| キー | 型 | 説明 |
|---|---|---|
| `enemy_type` | string | `"Drone"` / `"Carrier"` / `"Rusher"`（今後 `"Barrier"` / `"Bomb"`） |
| `prefab` | string | プレハブ名（`Resources/Json/Prefabs/<name>.json` から拡張子を除いたもの） |
| `trigger_t` | float | スポーンする t 値（0.0〜1.0） |
| `retreat_t` | float | 退避を始める t 値。`-1` で退避なし（Rusher 等） |
| `spline_id` | string | 移動・登場に使うスプラインの **Name** |
| `speed` | float | スプライン進行速度 [t/sec]。`0.15` で約 6.7 秒かけて踏破 |

### 3.3 種別ごとの追加フィールド

#### Drone（雑魚）

| キー | 型 | 説明 |
|---|---|---|
| `shoot_interval_t` | float | プレイヤー方向への射撃間隔 [t]。`0.04` で t が 0.04 進むごとに 1 発 |

```json
{
  "enemy_type": "Drone",
  "prefab": "drone",
  "trigger_t": 0.05,
  "retreat_t": 0.20,
  "spline_id": "EnemyPath_01",
  "speed": 0.15,
  "shoot_interval_t": 0.04
}
```

#### Carrier（運び屋）

| キー | 型 | 説明 |
|---|---|---|
| `spawn_interval_sec` | float | ザコ生成の間隔 [秒]。**こちらは t ではなく秒** |
| `spawn_limit` | int | 同時生成数の上限 |

```json
{
  "enemy_type": "Carrier",
  "prefab": "carrier",
  "trigger_t": 0.35,
  "retreat_t": 0.55,
  "spline_id": "EnemyPath_01",
  "speed": 0.05,
  "spawn_interval_sec": 4.0,
  "spawn_limit": 3
}
```

#### Rusher（突進型）

追加フィールドなし。スプラインに乗って登場 → 終端で 1.5 秒溜め → プレイヤー方向へ突進 → 画面外で消滅。
**`retreat_t` は `-1` を指定**（退避処理を使わない）。

```json
{
  "enemy_type": "Rusher",
  "prefab": "rusher",
  "trigger_t": 0.60,
  "retreat_t": -1.0,
  "spline_id": "EnemyPath_01",
  "speed": 0.40
}
```

---

## 4. 動作の細かい挙動

### 4.1 t 進行と Seek

- カメラ進行度 t は **秒数 × `railCameraSpeed_`** で計算される（[StagePlayScene.h:61](Project/DirectXGame/Game/Scene/StagePlayScene.h#L61) 参照）
- シーク（巻き戻し）すると `t < trigger_t` に戻った敵は **消滅してスポーン待ち状態に戻る**
- 退避中の敵を `t < retreat_t` まで戻すと **退避がキャンセルされる**（再スポーンも自動で行われる）

### 4.2 ビルボード（プレイヤー方向を向く）

- Drone・Rusher（溜め中）はクォータニオン回転でプレイヤー方向を向く
- Carrier・Rusher（突進中）は固定向き

### 4.3 退避の挙動

- Drone: `retreat_t` 到達で `RetreatCommand` が発動し、初期方向 `(0, 0.8, -0.6)` で画面後方上空へ離脱
- Carrier: 同上
- Rusher: 退避なし。突進終了で勝手に消える

退避方向や速度を変えたい場合は [RetreatCommand.h](Project/DirectXGame/Game/Enemy/Commands/RetreatCommand.h) のデフォルト値を編集する。

### 4.4 プレハブを差し替える

- `Resources/Json/Prefabs/drone.json` などを編集すると見た目・HP・コライダーが変わる
- `kind` を `"Animated"` にしてモデルファイルを指定すれば 3D モデルにも対応

---

## 5. よくある作業

### 5.1 スプラインを追加して敵を別経路で出す

1. シーンエディタで `EnemyPath` を追加
2. `Inspector` で名前を `EnemyPath_02` 等に変更
3. 制御点を配置
4. シーンを Save
5. `stage1.json` に `"spline_id": "EnemyPath_02"` で敵を追加

### 5.2 射撃頻度を上げる

`shoot_interval_t` を小さくする。0.02 だと t が 0.20 進む間に 10 発。

### 5.3 同時に複数の敵を出す

同じ `trigger_t` で複数のエントリを並べる。各エントリは独立に管理されるので、別スプラインに乗せると同時編隊飛行になる。

### 5.4 ボス前に静かな区間を作る

`retreat_t` を早めに切って、次の `trigger_t` まで間隔を空ける。

---

## 6. デバッグ

- `LogBuffer`（ImGui ウィンドウ）にスポーン失敗時の警告が出る
  - `Wave: spline not found: XXX` → `spline_id` のタイポか、シーンに該当名スプラインがない
  - `SpawnEnemyOnSpline: prefab not found` → `prefab` 名のタイポか、JSON が無い
- シークバーで時間を巻き戻して同じ箇所を何度もテスト可能

---

## 7. 関連ファイル

- 仕様書全体: [GameSpec.md](GameSpec.md) §10
- スポーンシステム実装: [BaseScene.cpp](../DirectXGame/Game/Scene/BaseScene.cpp), [StagePlayScene.cpp](../DirectXGame/Game/Scene/StagePlayScene.cpp)
- コマンドパターン: [Game/Enemy/](../DirectXGame/Game/Enemy/)
- Wave 定義 IO: [WaveDef.h](../DirectXGame/Game/Wave/WaveDef.h)

# 25. 自機のアニメの作り方（まとまり別の手順書）

自機（PlayerModel）のゲーム中のアニメを、**まとまり（A 近接／B 移動・回避／C 射撃／D 被弾・回復・盾／E 必殺技）ごとに別々のチャットで同時に**作るための手順書。**各まとまりのチャットは最初にこれを読む。**

**状態**：2026-11-27 土台を作った。Python の分け方・まとまり単位の書き出し・cook は動作確認済み（既存の 9 本と .mesh/.skel/.mat はバイト単位で同じ）。お試しの `BossIdle` は書き出し・cook 済み。ゲーム側（PlayerAnimPart）は**ビルド・実機未確認**。
2026-11-28 A（近接）：B1〜B4・Y1〜Y4・溜め（B4 から／Y4 から）の 12 本を書き出し・cook し、MeleeCombo.json に入れた（実機未確認）。単独の溜めは未。振りは melee.py の `melee_pose`（胸の水平の向きから見た手首の位置・刃の向き・振る向き）で書く。

- C 射撃：2026-11-27 7 本（Shoot・ChargeRelease・PrecisionAim とそれぞれの …Rail、ChargeHold）を書き出し・cook。PlayerAnimPart に `railClip` / `groundClip`（§5.1）を足した。**ビルド・実機未確認**。

- **B 移動・回避**：2026-11-27 14 本を書き出し・cook・`move.json` に入れた（動画でユーザー了承、**実機未確認**）。STG＝`RailStand`（起こした立ち、ループ）・`RailGlide`（約 70° 前へ倒す滑空、ループ）・`RailDodgeLeft/Right`（逆へひねる予兆→一回転）・`Up`（縮む→反る）・`Down`（まっすぐ突っ込む）。地上＝`BossIdle`（**クリップは直立に近い `BossStandIdle` に変えた。姿勢の `BossIdle`＝腰を落とした構えは A・C が元に使うので変えていない**）・`BossRunForward`（2026-11-28 前傾して床から 10cm 浮くダッシュに変更。右手は後ろ・左ひじ約 100°・足の裏は後ろ）・`BossRunLeft/Right`（スキーのターン。自機は入力の向きへ回るので回っている間に鳴る）・`GroundDodgeLeft/Right`（しゃがむ予兆→弧を描く一回転、0.4 秒）・`Forward`（飛び込みきりもみ→前転）・`Back`（しなるバック宙）は 0.6 秒（回避のクールダウン）。後ろのスライドは作らない（`bossRunBack` は待機で代用）。**2026-11-28（ユーザー）**：地上のチャージの構え `chargeHold` は鳴らさない（撃たずにいると 3 秒で溜まり、移動を隠し続けていた。`SyncPlayerAnim` から外した）。ボス戦で撃ってから 1 秒（`moveStateDelay`）と精密射撃の間は、自機がカメラの向きへ回る（`UpdateBossPlayerFacing` の `aimFacing`）。撃ちながら下がると「後ろ」が出るので、`bossRunBack` は待機で代用。すべての動きに大げさな予兆を入れる（ユーザー）。

作るクリップの一覧は 8 番 §2。近接の技の決まりは 24 番。

---

## 1. 全体の流れ

```
player_clips/<まとまり>.py に姿勢とクリップを書く
  → rig_player_mech.py --group <まとまり> --video で動画を見て直す
  → --group <まとまり> --export で PlayerModel@<まとまり>.gltf に書き出す
  → cook_assets.py --only ... で Resources/.../PlayerModel_<クリップ>.anim を作る
  → JSON（PlayerAnim/<まとまり>.json か MeleeCombo.json）にクリップの名前を書く
  → ゲームで鳴る（Debug は StagePlay Tuning の「Player Anim (25)」で JSON を読み直し・試し鳴らしできる）
```

- クリップは **Claude がスクリプトでキーを作る**（Mixamo には良いものが少ない）。今までの 9 本と同じ作り方。
- 体のボーンは Mixamo の名前（`mixamorig:Hips` など）で T ポーズ。羽根は別モデル（ここでは扱わない）。
- ゲームのアニメは 1 本ずつ再生する（重ねる仕組みは無い）。クリップは体全体を書く。
- **その場で動く**（腰を前後にずらしてよいが、体ごと進む動きは付けない）。移動はゲームが動かす。

---

## 2. ファイルの置き場所

| 何 | 場所 | 持ち主 |
|---|---|---|
| 姿勢・クリップの表 | `tools/BlenderPipeline/player_clips/<まとまり>.py` | そのまとまりのチャット |
| 書き出した glTF | `Assets/Models/Player/PlayerModel/PlayerModel@<まとまり>.gltf` と `.bin` | そのまとまりのチャット |
| cook した .anim | `Resources/Models/Player/PlayerModel/PlayerModel_<クリップ>.anim` | そのまとまりのチャット |
| 鳴らし方の JSON | `Resources/Json/Tuning/PlayerAnim/<まとまり>.json`（近接は `Resources/Json/Tuning/MeleeCombo.json`） | そのまとまりのチャット |
| 共通の仕組み | `rig_player_mech.py`・`cook_assets.py`・`PlayerAnimPart`・`common.json`・`StagePlayScene` | 土台（直す時は §6） |

まとまりのファイル名：

| まとまり | Python | JSON |
|---|---|---|
| （既存）スタート演出・HUB・死亡演出の 9 本 | `start.py` | なし（StageStartPart・HubMechModel・PlayerDeathPart が直接鳴らす） |
| A 近接 | `melee.py` | `MeleeCombo.json`（技ごとの `clip` / `holdClip`） |
| B 移動・回避 | `move.py` | `PlayerAnim/move.json` |
| C 射撃 | `shoot.py` | `PlayerAnim/shoot.json` |
| D 被弾・回復・盾 | `defense.py` | `PlayerAnim/defense.json` |
| E 必殺技 | `special.py` | `PlayerAnim/special.json` |
| 後で（着地・勝利・STG の死亡） | まだ無い | `PlayerAnim/common.json` |

---

## 3. 姿勢とクリップの書き方（`player_clips/<まとまり>.py`）

ファイルの中身は 2 つの表だけ。

```python
POSES = {
    "MeleeB1Wind": {               # 姿勢の名前（全部のまとまりで重ならないこと）
        "base": "BossIdle",         # ほかの姿勢を元にして、書いた項目だけ置き換える（ほかのまとまりの姿勢でもよい）
        "hips": {"offset": (0.0, 0.02, -0.08), "lean": 6.0},
        "bend": {"Spine": 4.0, "Spine1": 4.0},
        "rotate": [("Spine1", (0.0, 0.0, 1.0), 25.0)],          # 上体をひねる
        "arms": {"Right": {"Arm": (-0.3, 0.4, -0.6), "ForeArm": (-0.2, -0.8, 0.1)},
                 "Left": {"Arm": (-0.3, -0.12, -1.0), "ForeArm": (0.05, -1.0, 0.15)}},
    },
}
CLIPS = {
    "MeleeB1": {"duration": 0.45, "keys": [(0.0, "BossIdle"), (0.12, "MeleeB1Wind"),
                                           (0.25, "MeleeB1Hit", "linear"), (0.45, "BossIdle")]},
    "MeleeChargeHold": {"duration": 1.0, "pose": "MeleeChargeHoldPose"},   # ループ
}
```

### 3.1 向き・単位

- アーマチュアの空間：**正面は −Y、+X がモデルの左、+Z が上**。単位は m・度。
- 腕・脚の向きは**右側で書く**。左は x を反転して使う（左右別に書く時も、どちらも右側の書き方）。
- 体の全高は 1.69m。ゲームではボス戦も STG も大きさ 1（カタパルトだけ 18m に拡大）。

### 3.2 姿勢の項目

| 項目 | 意味 |
|---|---|
| `base` | ほかの姿勢の名前。その値を元にして、書いた項目だけ置き換える |
| `hips` | `offset`（腰のずらし [m]）と `lean`（前傾 [度]） |
| `bend` | `{ボーン: 度}`。X 軸まわり（正＝前へ倒す）。Spine / Spine1 / Spine2 / Neck / Head |
| `rotate` | `[(ボーン, 軸, 度), …]`。bend の後・腕の前にボーンを頭のまわりに回す（子も回る）。ひねりは背骨・腰に |
| `arms` | `{"Arm"/"ForeArm"/"Hand": 向き}`。左右同じなら直接、別なら `{"Right": {...}, "Left": {...}}` |
| `legs` | `{"ik": True, "knee": ひざを出す側, "toe_out": 度, "heel_lift": 度}`（足首を床の決まった位置に置く）か、`{"ik": False, "Right": {"UpLeg"/"Leg"/"Foot": 向き}, "Left": {...}}`。省くと直立の脚 |
| `ankle_lean` | 足首を支点に体全体を前へ倒す [度] |
| `tilt` | 最後に腰のまわりで脚ごと前へ倒す [度] |
| `ground` | 最後に、いちばん低い所が床に来るよう上下させる |
| `sway` | ループの小さな動き（`spine`・`hips_z`・`arm`・`leg`） |
| `arm_ik` | 右手を決まった位置へ（HUB の乗り込み用） |
| `rotate_after` | `rotate` と同じ書き方で、いちばん最後に回す（手首のひねり・腕ごと振る） |
| `call` | 関数 `f(arm, rig, phase)`。いちばん最後に呼ぶ。上で足りない動きは**自分のファイルの中に関数を書いて**ここで呼ぶ（`rig.rotate_pose_bone` などが使える） |

ボーンの名前は `mixamorig:` を除いた名前（`Hips`・`Spine`・`Spine1`・`Spine2`・`Neck`・`Head`・`RightShoulder`・`RightArm`・`RightForeArm`・`RightHand`・`RightUpLeg`・`RightLeg`・`RightFoot` と Left）。肩の装甲は腕の回転の 40% だけ自動で付いてくる。

### 3.3 クリップ

| 書き方 | 意味 |
|---|---|
| `{"duration": 秒, "pose": 姿勢}` | その姿勢のループ（`sway` があれば動く） |
| `{"duration": 秒, "keys": [(秒, 姿勢), …]}` | キーのあいだをつなぐ（位置は線形・回転は球面線形）。つなぎ方の既定は smoothstep（両端で速さ 0） |
| キーを `(秒, 姿勢, "in")` | 前のキーから加速していく（振り下ろし・力が抜けて落ちる） |
| キーを `(秒, 姿勢, "linear")` | 等速（振り抜きの途中で速さを殺さない） |

- 30fps の毎コマでキーを焼く。ループするかどうか・速度はゲームの JSON が決める。
- 1 回きりのクリップは**元の姿勢（`BossIdle` か `RailStand` など）から始めて元の姿勢へ戻る**と、前後のフェードがきれい。
- 近接は 24 番の決まりどおり、判定の出る時間（プレハブの `MeleeParams` の startup / activeDuration / recovery）に振りを合わせる（`clipSpeed` で合わせてもよい）。

---

## 4. コマンド（`Project/` で実行）

Blender は `C:/Program Files/Blender Foundation/Blender 4.4/blender.exe`（下では `blender`）。

```
# 確認の動画（クリップごとに正面・横・斜め前の mp4）。--clips で絞れる
blender -b --factory-startup -P tools/BlenderPipeline/rig_player_mech.py -- --body-clips Assets/Models/Player/PlayerModel/PlayerModel_rig.blend --group melee --clips MeleeB1,MeleeB2 --video <フォルダ>

# 確認の画像（姿勢ごとに 4 方向＋1 回きりのクリップを横から 7 コマ）
... --body-clips ... --group melee --preview <フォルダ>

# 書き出し（そのまとまりのクリップだけ。--clips とは一緒に使えない）
... --body-clips Assets/Models/Player/PlayerModel/PlayerModel_rig.blend --group melee --export Assets/Models/Player/PlayerModel

# cook（そのまとまりだけ）
python tools/Python/cook_assets.py --only Models/Player/PlayerModel/PlayerModel@melee
```

- 動画・画像はリポジトリの外（作業用のフォルダ）に出す。
- `--group` を付けずに `--export` すると止まる（ほかのまとまりの作りかけを混ぜないため。全部なら `--group all`）。
- `--mesh` はメッシュとスケルトンの `PlayerModel.gltf`（クリップなし）を書き出す。**モデルを変えた時だけ**。法線マップの `*Soft.png` も書き直すので、cook で体のテクスチャも作り直しになる（中身は同じ）。
- 書き出しは数十秒。Blender の `.blend` は上書きしない。
- cook は書き出したまとまりの `.anim` だけを書く。**.mesh / .skel / ほかのまとまりの .anim には触らない**（古いクリップも消さない。消したクリップの .anim は手で消す）。

---

## 5. ゲームで鳴らす（JSON）

### 5.1 B〜E と後で：`Resources/Json/Tuning/PlayerAnim/<まとまり>.json`

```json
{
  "states": {
    "bossIdle": { "clip": "BossIdle", "loop": true, "fade": 0.2, "priority": 0 },
    "bossRunBack": { "clip": "", "loop": true, "fade": 0.2, "priority": 0, "fallback": "bossRunForward" },
    "railStand": { "clip": "Launch", "loop": false, "fade": 0.0, "priority": 0, "startAt": "end" },
    "shoot": { "clip": "Shoot", "loop": false, "fade": 0.05, "priority": 30, "length": 0.2 }
  }
}
```

| 項目 | 意味 |
|---|---|
| `clip` | クリップの名前（`PlayerModel_<名前>.anim`）か `.anim` のパス。**空ならその状態は鳴らない**（`fallback` へ） |
| `railClip` / `groundClip` | STG / 地上で `clip` の代わりに鳴らすクリップ（空・省くと `clip`）。1 本のクリップは脚まで書くので、STG と地上の両方で鳴る状態（射撃など）は分けて作る。同じ状態のままモードが替わっても、クリップが替われば鳴らし直す（2026-11-27、C のチャットで追加） |
| `baseClips` | `{ "<土台の状態>": "<クリップ>", … }`。今の土台（モードと移動から選ぶ状態。クリップが空で fallback する `bossRunBack` などもそのままの名前）に合うものを `railClip` / `groundClip` より先に鳴らす。動きながら撃つ時の脚に合わせる（2026-11-28、C のチャットで追加） |
| `loop` | 繰り返すか |
| `speed` | 再生速度（既定 1） |
| `fade` | この状態へ移るときのフェード [秒]（省くと common.json の `defaultFade`） |
| `priority` | 優先度（大きいほど勝つ。同じなら後から来た方） |
| `startAt` | 最初のコマの時刻 [秒]、または `"end"`（最後のコマで止める） |
| `length` | 1 回きりの長さ [秒]（省くとクリップの長さ ÷ 速度。短く切りたい時に） |
| `fallback` | クリップが空の時に代わりに使う状態（その状態の設定をまるごと使う） |

状態の一覧（キー・種類・誰が鳴らすか）：

| まとまり | 状態 | 種類 | いつ |
|---|---|---|---|
| B | `railStand` / `railGlide` | 土台 | STG。滑空はチャージ 1 段目が溜まった後（8 番 §2.2.1）、撃つと立ち姿勢へ |
| B | `bossIdle` / `bossRunForward` / `bossRunBack` / `bossRunLeft` / `bossRunRight` | 土台 | 地上。自機の向きから見た移動の向き（`runThreshold` 以上で走り） |
| B | `railDodgeNeutral/Left/Right/Up/Down` | 1 回きり | STG の回避（画面の向き。入力なしは Neutral） |
| B | `groundDodgeNeutral/Forward/Back/Left/Right` | 1 回きり | 地上の回避（自機の向きから見た向き） |
| C | `shoot` / `chargeRelease` | 1 回きり | 撃った時（チャージが溜まっていれば chargeRelease）。連射で鳴り直す |
| C | `chargeHold` | 押している間 | **地上だけ**。チャージ 1 段目が溜まった後（STG は滑空がチャージの見た目） |
| C | `precisionAim` | 押している間 | 精密射撃のボタンを押している間 |
| D | `railHit` / `groundHit` | 1 回きり | 被弾 |
| D | `heal` | 1 回きり | 回復した時 |
| D | `shieldGuard` | 押している間 | **構える操作がまだ無い**。D で呼ぶ所を決める（§6） |
| E | `stormLock` | 押している間 | Storm Lock の発動中ずっと |
| E | `shatterlineCharge` / `shatterlineSlash` / `shatterlineRecover` | 押している間 | Shatterline の段（振り抜きは崩壊の間も） |
| 後で | `landingTouchdown` / `victory` / `railDeath` | 1 回きり | 着陸で足場に着いた時 / ボスを倒した時 / STG で死んだ時（鳴らした後は死亡演出に任せる） |

優先度の目安（`common.json`）：土台 0 ／押している間 20〜25 ／ 1 回きり 30〜45 ／**近接 50（`meleePriority`）**／被弾 60 ／必殺技 70 ／勝利 80 ／死亡 90。1 回きりと近接は、上に隠れている間も時間が進み、一番上に戻ったら続きのコマから鳴る。ボス戦の死亡（Kneel）は PlayerDeathPart が直接鳴らす（その間 PlayerAnimPart は止まる）。

### 5.2 A 近接：`Resources/Json/Tuning/MeleeCombo.json`

技ごとの `clip` に `.anim` のパス（`Resources/Models/Player/PlayerModel/PlayerModel_MeleeB1.anim`）、速さは `clipSpeed`。溜めの構えは `charge.<種類>.holdClip`。フェードは `clipFade`。近接の間の優先度は `common.json` の `meleePriority`。技の段が終わって次の段を待つ時間が切れると、土台の姿勢へ戻る。

### 5.3 確かめ方

- Debug の「StagePlay Tuning」→「Player Anim (25)」：今の状態・鳴っている 1 回きり・読み込みの警告（クリップが無い・知らない状態・2 つのファイルに同じ状態）。**Reload PlayerAnim JSON** で読み直し、**test state** を選んで **Trigger**（1 回きり）・**Hold**（押している間）で、ゲームの条件を作らずに鳴らせる。
- ビルドはユーザーが Visual Studio で行う。

---

## 6. 衝突しないための決まり

1. **自分のまとまりのファイルだけ直す**：`player_clips/<自分>.py`、`PlayerModel@<自分>.gltf/.bin`、自分のクリップの `.anim`、`PlayerAnim/<自分>.json`（A は `MeleeCombo.json` の `clip`・`holdClip`・`clipSpeed`）。
2. **名前は全部のまとまりで重ならない**こと。クリップと姿勢の名前の頭にまとまりの印を付ける（A＝`Melee…`、B＝`Rail…`/`Boss…`/`Ground…`、C＝`Shoot…`/`Charge…`/`Precision…`、D＝`Hit…`/`Heal…`/`Shield…`、E＝`StormLock…`/`Shatterline…`）。重なるとスクリプトが止まる。
3. **ほかのまとまりの姿勢は使ってよいが直さない**（例：`start.py` の `Stand`、`move.py` の `BossIdle`）。元の姿勢が変わると自分のクリップの始まりが変わるので、`BossIdle` を B が作り直した時は、1 回きりのクリップを書き出し直すと前後がそろう。
4. **共通の仕組み（`rig_player_mech.py` の pose_body など・`cook_assets.py`・`PlayerAnimPart`・`common.json`）は直さない**。足りない動きは `call` で自分のファイルに書く。状態が足りない・鳴らすきっかけが足りない時は、自分では足さずにユーザー（か土台のチャット）に相談する。
5. **`StagePlayScene` は触らない**。行動の始まり・終わりを伝える口はこの回で付けてある（§5.1 の「いつ」）。
6. 書き出しは必ず `--group <自分>`。`--group all` と `--mesh` は使わない（ほかのまとまりの作りかけが混ざる・ほかのチャットの書き出しを上書きする）。
7. cook は `--only Models/Player/PlayerModel/PlayerModel@<自分>` で自分のまとまりだけ。
8. ビルド・Git 操作はしない（ユーザーが行う）。新しいファイルを作る前に相談する。

---

## 7. 仕組みのメモ（土台）

- `rig_player_mech.py --body-clips`：`player_clips` の全部のまとまりを読み（名前の重なりを確かめる）、選んだまとまりのクリップだけを焼いて NLA のトラックに並べ、**アーマチュアだけ**を glTF に書き出す。トラックの名前＝クリップの名前＝glTF のアニメの名前。
- `.anim` はジョイントの名前でキーを持つので、メッシュの無い glTF から作っても `PlayerModel.skel` で鳴る。cook は名前に `@` の付く glTF を「クリップだけ」とみなし、`<@の左>_<クリップ>.anim` だけを書く。
- 2026-11-27 に分けた時、既存の 9 本・`.mesh`・`.skel`・`.mat` が前と**バイト単位で同じ**ことを確かめた。`PlayerModel.gltf` はクリップなし（メッシュとスケルトンだけ）になり、9 本は `PlayerModel@start.gltf` から作る。
- ゲーム側：`Game/Scene/PlayerAnim/PlayerAnimPart`。`StagePlayScene` は `SyncPlayerAnim`（毎フレーム：モード・滑空・走りの向き・押している間）と、各行動の所の `Trigger`（射撃・回避・被弾・回復・着地・勝利）、`BeginPlayerDeath`（止める）を呼ぶ。近接は `IMeleeComboHost::PlayComboClip` / `RestoreComboPose` → `PlayMelee` / `EndMelee`。`ApplyPlayerPose` はフェーズが替わった時の鳴らし直し。
- Shatterline の段は `ShatterlinePart::GetPoseStage()`（Charge / Slash〈崩壊も〉/ Recover）。
- ジャスト回避の分身（`kJdClonePoseClip`）・スタート演出（StageStartPart）・HUB（HubMechModel）・ボス戦の死亡（PlayerDeathPart の Kneel）は今までどおり自分で鳴らす。
- 自機のアニメは**自機の時間（TimeGroup::Player）**で進む（`AnimatedObject3DInstance::SetTimeGroup`、2026-11-27）。ジャスト回避のスロー・近接の派生でワールドが遅い・止まっている間も等速で動く。`PlayerAnimPart` の時計も同じ時間。
- ジャスト回避の派生を選ぶと、元の回避のアニメは止める（`StagePlayScene::StopDodgeAnim`）。追加回避は新しい回避として最初から、回復は `heal`、近接は強・弱とも 4 段の自動連撃（Y1〜Y4 / B1〜B4 のクリップ）。選ばなければ元の回避の続きが鳴る。

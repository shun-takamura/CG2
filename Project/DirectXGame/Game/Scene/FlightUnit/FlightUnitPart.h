#pragma once
#include "Score/LoadoutState.h"
#include "BoneSocket.h"
#include "TimeGroup.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "MathUtility.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GameSession;
class AnimatedObject3DInstance;
class Camera;
class DirectXCore;
class IImGuiEditable;
class JsonValue;
class Object3DInstance;
class Object3DManager;
class SpriteInstance;
class SpriteManager;

/// <summary>
/// 飛行ユニットがシーンへ頼むことだけを並べた窓口（IRailStageHost と同じ流儀。公開 API は増やさない）。
/// GetPlayer / GetTimeScale / SetTimeScale は IRailStageHost と同じ形なので、StagePlayScene の 1 つの override で両方を満たす。
/// </summary>
class IFlightUnitHost {
public:
	virtual ~IFlightUnitHost() = default;
	/// <summary>シーンをまたいで残るプレイの状態を取得する（スコア・難易度・手に入れた物など）</summary>
	/// <returns>プレイの状態</returns>
	virtual GameSession* GetGameSession() const = 0;
	/// <summary>自機を取得する</summary>
	/// <returns>自機。いなければ nullptr</returns>
	virtual IImGuiEditable* GetPlayer() const = 0;
	/// <summary>時間グループの進む速さの倍率を返す</summary>
	/// <param name="g">対象の時間グループ</param>
	/// <returns>倍率（1 で通常）</returns>
	virtual float GetTimeScale(TimeGroup g) const = 0;
	/// <summary>時間グループの進む速さの倍率を設定する</summary>
	/// <param name="g">対象の時間グループ</param>
	/// <param name="scale">倍率（0 で停止、1 で通常）</param>
	virtual void SetTimeScale(TimeGroup g, float scale) = 0;
	/// <summary>自機の体の中心（ファンネル・バリアの基準）を返す</summary>
	/// <returns>ワールド座標</returns>
	virtual Vector3 GetFlightUnitAnchor() const = 0;
	/// <summary>レティクルが重なっている敵を返す</summary>
	/// <returns>敵。無ければ nullptr</returns>
	virtual IImGuiEditable* GetReticleTarget() const = 0;
	/// <summary>レティクルの狙いの点を返す</summary>
	/// <returns>ワールド座標</returns>
	virtual Vector3 GetReticleAimPoint() const = 0;
	/// <summary>画面に映っている敵（Enemy / Boss。敵弾は含めない）を集める</summary>
	/// <param name="out">敵を追加する先</param>
	virtual void CollectOnScreenEnemies(std::vector<IImGuiEditable*>& out) = 0;
	/// <summary>敵がまだ生きているかを返す</summary>
	/// <param name="e">調べる敵</param>
	/// <returns>生きていれば true</returns>
	virtual bool IsEnemyAlive(IImGuiEditable* e) const = 0;
	/// <summary>自機の通常弾と同じ弾を撃つ</summary>
	/// <param name="from">撃ち出す位置（ワールド）</param>
	/// <param name="dir">撃つ向き（ワールド）</param>
	/// <param name="homingTarget">強ホーミングで追う敵。nullptr ならまっすぐ飛ぶ</param>
	/// <param name="damageScale">自機の攻撃力に掛ける倍率</param>
	virtual void FireSupportBullet(const Vector3& from, const Vector3& dir, IImGuiEditable* homingTarget, float damageScale) = 0;
	/// <summary>球の中の敵弾を消し、そのダメージの合計を返す</summary>
	/// <param name="center">球の中心（ワールド）</param>
	/// <param name="radius">球の半径 [m]</param>
	/// <returns>消した敵弾のダメージの合計（難易度の倍率の前）</returns>
	virtual int AbsorbEnemyBulletsInSphere(const Vector3& center, float radius) = 0;
	/// <summary>ファンネル大のビームを撃ち始める（撃っている間は SetSupportBeamPose で根元と向きを付け直す）</summary>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド）</param>
	/// <param name="damageScale">自機の攻撃力に掛ける倍率</param>
	/// <returns>ビームの番号（0 なら撃てなかった）</returns>
	virtual uint32_t FireSupportBeam(const Vector3& from, const Vector3& dir, float damageScale) = 0;
	/// <summary>ファンネル大のビームの根元と向きを付け直す</summary>
	/// <param name="id">FireSupportBeam が返した番号</param>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド）</param>
	virtual void SetSupportBeamPose(uint32_t id, const Vector3& from, const Vector3& dir) = 0;
	/// <summary>ファンネル大のビームが撃っている途中かを返す</summary>
	/// <param name="id">FireSupportBeam が返した番号</param>
	/// <returns>途中なら true</returns>
	virtual bool IsSupportBeamActive(uint32_t id) const = 0;
};

/// <summary>
/// 飛行ユニットの系統ごとの中身（常時の効果＋RB の技）。系統ごとに 1 つ。段階（1..3）で値が変わる。
/// dt はすべて実時間（UI グループ）。RB のスローで World が遅くなっても技の時間・クールタイムは等速で減る。
/// </summary>
class FlightUnitSkill {
public:
	virtual ~FlightUnitSkill() = default;

	/// <summary>シーンへの窓口とカメラを受け取る</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">カメラ（右・上・前の向きに使う）</param>
	virtual void Initialize(IFlightUnitHost* host, Camera* camera) { host_ = host; camera_ = camera; }
	/// <summary>段階を設定する</summary>
	/// <param name="level">段階 1〜3</param>
	void SetLevel(int level) { level_ = level; }
	/// <summary>技を動かさずにモデルだけ見せるかを設定する（HUB の機体設定）。狙いを追う物は付ける所の向きのまま</summary>
	/// <param name="preview">true でモデルだけ見せる</param>
	void SetModelPreview(bool preview) { modelPreview_ = preview; }

	/// <summary>RB の技を出す（クールタイムは技が終わってから数える）</summary>
	/// <returns>出せたら true</returns>
	virtual bool Activate() = 0;
	/// <summary>技を途中で終える</summary>
	/// <param name="restoreTime">false のときは時間の倍率に触らない（ジャスト回避・必殺技が握っている）</param>
	virtual void Cancel(bool restoreTime) = 0;
	/// <summary>技を出している途中かを返す</summary>
	/// <returns>途中なら true</returns>
	virtual bool IsActive() const = 0;
	/// <summary>技が時間の倍率を使うかを返す（ジャスト回避・必殺技が起きたら終わらせる）</summary>
	/// <returns>使うなら true</returns>
	virtual bool UsesTimeScale() const { return false; }
	/// <summary>技の残りを返す（表示用）</summary>
	/// <returns>1→0</returns>
	virtual float GetActiveRatio() const = 0;
	/// <summary>今の段階のクールタイムを返す</summary>
	/// <returns>クールタイム [秒]</returns>
	virtual float GetCooldown() const = 0;
	/// <summary>常時の効果と技を進める</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">操作できるか（必殺技・ジャスト回避・死亡演出・着地の間は false）</param>
	virtual void Update(float dt, bool canAct) = 0;
	/// <summary>自機が撃った（攻撃系の常時の支援射撃が自機の射撃に合わせて撃つ）</summary>
	/// <param name="canAct">操作できるか</param>
	virtual void OnPlayerShot(bool canAct) { (void)canAct; }
	/// <summary>技の 3D の見た目を描く</summary>
	virtual void Draw() {}

	// ----- 背中のモデル（バックパックと、それに付ける物。20 番 §3.1）-----
	/// <summary>バックパックを背負う系統かを返す（付けている間は羽根を隠す）</summary>
	/// <returns>背負うなら true</returns>
	virtual bool UsesBackpack() const { return false; }
	/// <summary>バックパック本体を描くかを返す（防御系・機動系は位置の基準にだけ使い、本体は描かない）</summary>
	/// <returns>描くなら true</returns>
	virtual bool DrawsBackpack() const { return true; }
	/// <summary>背中に付ける物のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	virtual void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) { (void)object3DManager; (void)dxCore; }
	/// <summary>バックパックの行列が決まった後（体のスケルトンの更新の後）に、付けている物の行列を作る</summary>
	/// <param name="backpackWorld">バックパックのワールド行列</param>
	virtual void UpdateModels(const Matrix4x4& backpackWorld) { (void)backpackWorld; }
	/// <summary>付けている物を不透明のパスで描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="idPass">true なら ID マスクへ描く（HUB の機体設定のノイズを機体だけ・機体以外にかけるため）</param>
	virtual void DrawModels(DirectXCore* dxCore, bool idPass) { (void)dxCore; (void)idPass; }

	// ----- 常時の効果 -----
	/// <summary>攻撃力の補正を返す（攻撃系。20 番 §3.4）</summary>
	/// <returns>攻撃力に掛ける倍率</returns>
	virtual float GetAttackScale() const { return 1.0f; }
	/// <summary>HP の上限の補正を返す（防御系）</summary>
	/// <returns>HP の上限に掛ける倍率</returns>
	virtual float GetMaxHpScale() const { return 1.0f; }
	/// <summary>移動速度の補正を返す（機動系）</summary>
	/// <returns>移動速度に掛ける倍率</returns>
	virtual float GetMoveSpeedScale() const { return 1.0f; }
	/// <summary>ジャスト回避のときに足す必殺技ゲージを返す</summary>
	/// <returns>足すゲージの量</returns>
	virtual float GetJustDodgeGaugeBonus() const { return 0.0f; }
	/// <summary>ジャスト回避の受付を延ばす秒を返す（StagePlay.json の dodge.justWindow に足す）</summary>
	/// <returns>延ばす秒 [秒]</returns>
	virtual float GetJustDodgeWindowBonus() const { return 0.0f; }
	/// <summary>被弾のダメージを補正する</summary>
	/// <param name="damage">難易度の倍率の後のダメージ</param>
	/// <returns>補正後のダメージ</returns>
	virtual int ModifyIncomingDamage(int damage) { return damage; }
	/// <summary>被弾を吸う</summary>
	/// <param name="damage">被弾のダメージ</param>
	/// <returns>吸ったら true（ジャスト回避もダメージも起きない）</returns>
	virtual bool AbsorbHit(int damage) { (void)damage; return false; }

	/// <summary>クールタイムのバーの色を返す</summary>
	/// <returns>色（RGBA 0〜1）</returns>
	virtual Vector4 GetColor() const = 0;
	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">この系統の欄</param>
	virtual void LoadTuning(const JsonValue& node) = 0;
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先（この系統の欄）</param>
	virtual void SaveTuning(JsonValue& node) const = 0;
	/// <summary>調整値の ImGui を表示する</summary>
	virtual void OnImGui() {}

	/// <summary>段階 1〜3 の値を JSON の配列（[段階1, 段階2, 段階3]）から読む（無い要素は今の値のまま）</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">読み込み先</param>
	static void ReadLevels(const JsonValue& node, const char* key, float (&v)[3]);
	/// <summary>段階 1〜3 の値を JSON の配列（[段階1, 段階2, 段階3]）にして書く</summary>
	/// <param name="node">書き込み先の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">書く値</param>
	static void WriteLevels(JsonValue& node, const char* key, const float (&v)[3]);
	/// <summary>段階 1〜3 の値を ImGui の DragFloat3 で調整する（Debug のみ）</summary>
	/// <param name="label">ラベル</param>
	/// <param name="v">調整する値</param>
	/// <param name="speed">ドラッグの速さ</param>
	/// <param name="minV">下限</param>
	/// <param name="maxV">上限</param>
	static void DragLevels(const char* label, float (&v)[3], float speed, float minV, float maxV);
	/// <summary>キーの値（要素 4 つの配列）を Vector4 として読む（配列でなければ今の値のまま）。色は FlightUnitPart の表示でも使う</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">読み込み先</param>
	static void ReadVec4(const JsonValue& node, const char* key, Vector4& v);
	/// <summary>Vector4 を要素 4 つの配列にしてキーへ書く</summary>
	/// <param name="node">書き込み先の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">書く値</param>
	static void WriteVec4(JsonValue& node, const char* key, const Vector4& v);
	/// <summary>キーの値（要素 3 つの配列）を Vector3 として読む（配列でなければ今の値のまま）</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">読み込み先</param>
	static void ReadVec3(const JsonValue& node, const char* key, Vector3& v);
	/// <summary>Vector3 を要素 3 つの配列にしてキーへ書く</summary>
	/// <param name="node">書き込み先の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="v">書く値</param>
	static void WriteVec3(JsonValue& node, const char* key, const Vector3& v);
	/// <summary>{ translate, rotate（ラジアン）, scale } の JSON オブジェクトを Transform として読む（無いキーは今の値のまま）</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	/// <param name="t">読み込み先</param>
	static void ReadTransform(const JsonValue& node, Transform& t);
	/// <summary>Transform を { translate, rotate（ラジアン）, scale } の JSON オブジェクトにする</summary>
	/// <param name="t">書き出す値</param>
	/// <returns>JSON オブジェクト</returns>
	static JsonValue WriteTransform(const Transform& t);
	/// <summary>付ける位置・角度（度で見せる。自分の軸まわりにも回せる）・大きさを ImGui で調整する（Debug のみ）</summary>
	/// <param name="id">ImGui の ID</param>
	/// <param name="t">調整する値</param>
	static void DragTransform(const char* id, Transform& t);
	/// <summary>X の面で鏡に映した置き方を作る（左右のパーツ・ユニット用。Rx·Ry·Rz の順なので Y・Z の回転を反転する）</summary>
	/// <param name="t">元の置き方</param>
	/// <returns>鏡に映した置き方</returns>
	static Transform MirroredX(const Transform& t);
	/// <summary>2 つの置き方を成分ごとに線形補間する</summary>
	/// <param name="a">k = 0 の置き方</param>
	/// <param name="b">k = 1 の置き方</param>
	/// <param name="k">補間の割合</param>
	/// <returns>補間した置き方</returns>
	static Transform LerpTransform(const Transform& a, const Transform& b, float k);

protected:
	/// <summary>段階を配列の添字にする</summary>
	/// <returns>0〜2（段階が範囲外なら端に寄せる）</returns>
	int LevelIndex() const { return level_ < 1 ? 0 : (level_ > kMaxLevel ? kMaxLevelIndex : level_ - 1); }
	/// <summary>カメラの右・上・前（ワールド）を返す（カメラが無ければワールドの軸）</summary>
	/// <param name="right">右の受け取り先</param>
	/// <param name="up">上の受け取り先</param>
	/// <param name="forward">前の受け取り先</param>
	void GetCameraBasis(Vector3& right, Vector3& up, Vector3& forward) const;

	IFlightUnitHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	int level_ = 1;
	bool modelPreview_ = false;

private:
	// 段階の最大（段階ごとの値の配列の長さ）
	static constexpr int kMaxLevel = 3;
	// 最大の段階の配列の添字
	static constexpr int kMaxLevelIndex = 2;
};

/// <summary>
/// 飛行ユニット（20_Loadout.md §3。サブ武器を兼ねる）の共通部分：付けている系統の切り替え・RB の入力・クールタイム・
/// 常時の効果の問い合わせ口・クールタイムの表示。中身は系統ごとの FlightUnitSkill（Assault / Guard / Mobility）。
/// 付けている系統と段階は LoadoutState が持つ（HUB の装備画面ができたらそこが書く）。シューティングとボス戦の両方で使う。
/// StagePlayScene は IFlightUnitHost を渡すだけで、技の中身は知らない。
/// </summary>
class FlightUnitPart {
public:
	/// <summary>3 系統の中身を作り、調整値を読む</summary>
	FlightUnitPart();
	~FlightUnitPart();

	/// <summary>系統ごとの中身・バックパック・クールタイムのバーを用意し、LoadoutState の系統・段階に付ける</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">カメラ</param>
	/// <param name="spriteManager">クールタイムのバーに使う（nullptr ならバーを出さない）</param>
	/// <param name="object3DManager">背中のモデルに使う（nullptr ならモデルを出さない）</param>
	/// <param name="dxCore">背中のモデルに使う（nullptr ならモデルを出さない）</param>
	void Initialize(IFlightUnitHost* host, Camera* camera, SpriteManager* spriteManager,
		Object3DManager* object3DManager, DirectXCore* dxCore);
	/// <summary>Initialize 済みかを返す</summary>
	/// <returns>済みなら true</returns>
	bool IsInitialized() const { return host_ != nullptr; }
	/// <summary>LoadoutState の系統・段階に付け直す（技は止め、クールタイムは 0 にする）</summary>
	void SyncLoadout();

	/// <summary>毎フレーム。技を進め、終わったらクールタイムを数え、RB で技を出す</summary>
	/// <param name="dt">実時間（UI）の経過 [秒]</param>
	/// <param name="triggered">RB が押されたフレームなら true</param>
	/// <param name="canAct">操作できるか</param>
	/// <param name="timeTaken">ジャスト回避・必殺技が時間を握っているか（スローの技はそこで終わる。20 番 §6）</param>
	void Update(float dt, bool triggered, bool canAct, bool timeTaken);
	/// <summary>自機が撃ったことを今の系統へ伝える（攻撃系の支援射撃を自機の射撃に合わせる）</summary>
	/// <param name="canAct">操作できるか</param>
	void NotifyPlayerShot(bool canAct);
	/// <summary>付けている系統の技の 3D の見た目を描く</summary>
	void Draw3D();
	/// <summary>クールタイムのバー（必殺技ゲージの下）を描く</summary>
	void DrawUI();
	/// <summary>背中のモデル（バックパックを body の Spine2 に付ける）の行列を作る。体のスケルトンの更新の後に呼ぶ</summary>
	/// <param name="body">自機の体（nullptr なら描かない）</param>
	void UpdateModels(AnimatedObject3DInstance* body);
	/// <summary>背中のモデルを不透明のパスで描く（自機が見えている間だけ呼ぶ）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void DrawModels(DirectXCore* dxCore);
	/// <summary>背中のモデルを ID マスクへ描く（HUB の機体設定）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void DrawModelsIdPass(DirectXCore* dxCore);
	/// <summary>付けている系統がバックパックを背負うかを返す（羽根を隠す）</summary>
	/// <returns>背負うなら true</returns>
	bool UsesBackpack() const;
	/// <summary>背中のモデルの大きさを設定する（付ける所のずらしはメートルなので、拡大した体に付けるときに掛ける。HUB の機体設定の 18m の機体）</summary>
	/// <param name="scale">倍率</param>
	void SetModelScale(float scale) { modelScale_ = scale; }
	/// <summary>技を動かさずにモデルだけ見せるかを全系統へ設定する（FlightUnitSkill::SetModelPreview）</summary>
	/// <param name="preview">true でモデルだけ見せる</param>
	void SetModelPreview(bool preview);
	/// <summary>系統ごとの中身を返す（装備画面が付けていない系統の値も出すため。段階は LoadoutState の値＝SyncLoadout で入る）</summary>
	/// <param name="kind">系統</param>
	/// <returns>中身。Wings・範囲外は nullptr</returns>
	const FlightUnitSkill* GetSkill(FlightUnitKind kind) const;
	/// <summary>技を止め、クールタイムを 0 にする（フェーズの切り替え）</summary>
	void Reset();

	// ----- 常時の効果（付けていなければ素の値）-----
	/// <summary>付けている系統の攻撃力の補正を返す</summary>
	/// <returns>倍率（付けていなければ 1）</returns>
	float GetAttackScale() const;
	/// <summary>付けている系統の HP の上限の補正を返す</summary>
	/// <returns>倍率（付けていなければ 1）</returns>
	float GetMaxHpScale() const;
	/// <summary>付けている系統の移動速度の補正を返す</summary>
	/// <returns>倍率（付けていなければ 1）</returns>
	float GetMoveSpeedScale() const;
	/// <summary>付けている系統のジャスト回避で足す必殺技ゲージを返す</summary>
	/// <returns>足す量（付けていなければ 0）</returns>
	float GetJustDodgeGaugeBonus() const;
	/// <summary>付けている系統のジャスト回避の受付の延長を返す</summary>
	/// <returns>延ばす秒 [秒]（付けていなければ 0）</returns>
	float GetJustDodgeWindowBonus() const;
	/// <summary>付けている系統で被弾のダメージを補正する</summary>
	/// <param name="damage">難易度の倍率の後のダメージ</param>
	/// <returns>補正後のダメージ（付けていなければそのまま）</returns>
	int ModifyIncomingDamage(int damage);
	/// <summary>付けている系統で被弾を吸う</summary>
	/// <param name="damage">被弾のダメージ</param>
	/// <returns>吸ったら true（ジャスト回避もダメージも起きない）</returns>
	bool TryAbsorbHit(int damage);

	/// <summary>付けている系統を返す</summary>
	/// <returns>系統</returns>
	FlightUnitKind GetKind() const { return kind_; }
	/// <summary>機動系の RB のスロー中かを返す（自機の弾は World でなく Player の時間で進める）</summary>
	/// <returns>スロー中なら true</returns>
	bool IsTimeSlowActive() const;

	/// <summary>調整値を Resources/Json/Tuning/FlightUnit.json から読む（系統ごと・バーの表示・バックパック）</summary>
	/// <returns>読めたら true。ファイルが無い・読めなければ false</returns>
	bool LoadTuning();
	/// <summary>調整値を Resources/Json/Tuning/FlightUnit.json へ書く</summary>
	void SaveTuning() const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>付けている系統の中身を返す</summary>
	/// <returns>中身。Wings なら nullptr</returns>
	FlightUnitSkill* Current() const;

	IFlightUnitHost* host_ = nullptr;
	SpriteManager* spriteManager_ = nullptr;
	std::array<std::unique_ptr<FlightUnitSkill>, static_cast<size_t>(FlightUnitKind::Count)> skills_;
	FlightUnitKind kind_ = FlightUnitKind::Wings;
	float cooldownTimer_ = 0.0f;   // 残り（0 で撃てる）
	bool wasActive_ = false;

	// ----- バックパック（背中。系統の物を付ける土台）-----
	std::unique_ptr<Object3DInstance> backpack_;
	BoneSocket backpackSocket_;
	std::string backpackBone_ = "mixamorig:Spine2";
	// 骨から見たずらし（骨のスケールは除いてある＝メートル）。装甲の面（モデルの +Z）を後ろへ向ける
	Transform backpackMount_{ { 0.5f, 0.5f, 0.5f }, { 0.0f, kPi, 0.0f }, { 0.0f, 0.0f, -0.2f } };
	bool backpackPlaced_ = false;   // 行列を作った（作る前は描かない）
	float modelScale_ = 1.0f;       // SetModelScale

	// ----- クールタイムのバー（必殺技ゲージの下）-----
	std::unique_ptr<SpriteInstance> barBg_;
	std::unique_ptr<SpriteInstance> barFg_;
	Vector2 barPos_{ 60.0f, 80.0f };
	Vector2 barSize_{ 180.0f, 8.0f };
	Vector4 barBgColor_{ 0.10f, 0.10f, 0.20f, 0.8f };
	float barCoolingAlpha_ = 0.35f;   // クールタイム中は系統の色を薄く
	float readyPulse_ = 0.0f;         // 撃てるようになった瞬間に一度光らせる
	float readyPulseDuration_ = 0.4f;
};

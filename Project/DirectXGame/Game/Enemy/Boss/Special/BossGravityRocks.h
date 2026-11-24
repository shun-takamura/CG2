#pragma once
#include "Fracture/FracturedMeshRenderer.h"
#include "Fracture/VoronoiFracture.h"
#include "Vector3.h"
#include <random>
#include <string>
#include <vector>

class Camera;
class DirectXCore;
class JsonValue;
class Object3DManager;
class SRVManager;

/// <summary>
/// ボスの必殺技 GravityOrb の岩（鬼武者のかぐや姫の流れ）。
/// ボスを中心に同じ半径の円周へ重ならないように岩を置き、1 個を「崩れない盾の岩」に選ぶ。
/// 地面から浮き上がる → 盾以外が空中で崩れる → 重力が戻って破片と盾が落ちる →
/// 閃光で破片を外へ吹き飛ばして消す → 盾も崩れて消える、を動かして描く。
/// 盾の判定（着弾地点から見て盾の影に入っているか）と、割れていない岩からのプレイヤーの押し出しも受け持つ。
/// 岩は 1 個のモデルを読み込み時にボロノイで割っておき、全部の岩で同じ割れ方を使う（向きを変えて同じに見せない）。
/// </summary>
class BossGravityRocks {
public:
	/// <summary>調整値（JSON の "rocks" に保存）</summary>
	struct Tuning {
		std::string modelPath = "Resources/Models/Rocks/BossRock_A.glb";
		int fragmentSeeds = 18;          // 1 個の岩の破片の数の目安
		int rockCount = 6;               // 円周に置く岩の数
		float ringRadius = 12.0f;        // ボスからの半径 [m]（アリーナに収まらなければ縮める）
		float rockScale = 2.6f;          // 岩の大きさ（モデルの包む半径 ≒ 1 に掛ける）
		float rockGap = 1.5f;            // 岩どうしの最小の隙間 [m]
		float hoverHeight = 6.0f;        // 浮いたときの地面からの高さ [m]
		float hoverHeightJitter = 1.5f;  // 浮く高さのばらつき [m]
		float riseStagger = 0.5f;        // 岩ごとの浮き始めのずれの最大 [s]
		float bobAmplitude = 0.25f;      // 浮いている間の上下の揺れ [m]
		float bobSpeed = 1.2f;           // 上下の揺れの速さ [rad/s]
		float driftSpin = 0.15f;         // 浮いている間の回転の速さ [rad/s]
		float crumbleSpeed = 1.5f;       // 空中で崩れたときに破片が離れる速さ [m/s]
		float crumbleSpin = 1.5f;        // 空中で崩れたときの破片の回転の速さ [rad/s]
		float antiGravity = 0.6f;        // 重力が戻るまでの破片の浮き上がる加速度 [m/s^2]
		float drag = 0.8f;               // 重力が戻るまでの破片の減速 [1/s]
		float gravity = 22.0f;           // 重力が戻った後の重力加速度 [m/s^2]
		float bounce = 0.25f;            // 地面で跳ね返る割合
		float friction = 0.6f;           // 地面に当たったときの横の速さを残す割合
		float sweepSpeed = 25.0f;        // 閃光で外へ飛ばす速さ [m/s]
		float sweepUp = 6.0f;            // 閃光で外へ飛ばすときの上向きの速さ [m/s]
		float sweepFadeTime = 0.8f;      // 飛ばした破片が消えるまで [s]
		float shieldCrumbleSpeed = 2.0f; // 盾が崩れるときに破片が離れる速さ [m/s]
		float shieldFadeDelay = 0.6f;    // 盾が崩れてから消え始めるまで [s]
		float shieldFadeTime = 1.2f;     // 盾の破片が消えるまで [s]
		float judgeRadiusScale = 0.85f;  // 盾の判定の半径（見た目の横幅に対する比。小さいほど厳しい）
		float collideRadiusScale = 0.8f; // 押し出しの半径（見た目の横幅に対する比）
		float playerRadius = 0.6f;       // プレイヤーの半径 [m]（判定と押し出し）
		float playerHeight = 2.0f;       // プレイヤーの高さ [m]（押し出しの縦の重なり）
	};

	/// <summary>岩のモデルを読んで割り、描画を用意する（読み込み時に 1 回）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	/// <param name="object3DManager">フォグを借りる</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager);
	/// <summary>描画の GPU リソースを解放する</summary>
	void Finalize();
	/// <summary>今の調整値で岩を割り直す（モデル・破片の数を変えたとき）</summary>
	void Rebuild();

	/// <summary>円周へ岩を置き、盾の岩を選ぶ。岩は地面の下に隠れた状態から始まる</summary>
	/// <param name="center">円の中心（ボスの真下）</param>
	/// <param name="groundY">地面の高さ</param>
	/// <param name="maxRadius">岩が出てよい中心からの半径（アリーナの半径。0 以下で制限なし）</param>
	/// <param name="rng">乱数</param>
	void Place(const Vector3& center, float groundY, float maxRadius, std::mt19937& rng);
	/// <summary>岩を地面から浮かせ始める</summary>
	/// <param name="duration">浮き切るまでの時間 [s]</param>
	void BeginRise(float duration);
	/// <summary>盾以外の岩を空中で崩す（破片はまだ浮いたまま）</summary>
	void Crumble();
	/// <summary>重力を戻す（破片と盾の岩が落ちる）</summary>
	void Drop();
	/// <summary>閃光で盾以外の破片を外へ吹き飛ばして消す</summary>
	/// <param name="origin">吹き飛ばしの中心（着弾地点）</param>
	void Sweep(const Vector3& origin);
	/// <summary>盾の岩を崩して消す</summary>
	void CrumbleShield();
	/// <summary>全部消す（中断・終了）</summary>
	void Clear();

	/// <summary>岩と破片を動かす</summary>
	/// <param name="dt">経過時間 [s]</param>
	void Update(float dt);
	/// <summary>岩と破片を描く（3D の描画の途中で呼ぶ）</summary>
	/// <param name="camera">カメラ</param>
	void Draw(const Camera& camera);

	/// <summary>岩がまだ残っているか</summary>
	/// <returns>描く物があれば true</returns>
	bool IsActive() const { return !rocks_.empty(); }
	/// <summary>盾の岩が着地したフレームか（1 回だけ true）</summary>
	/// <returns>着地したフレームなら true</returns>
	bool ConsumeShieldLanded();
	/// <summary>盾の岩の位置を返す</summary>
	/// <param name="out">盾の岩の中心の受け取り先</param>
	/// <returns>盾の岩があれば true</returns>
	bool GetShieldPosition(Vector3& out) const;

	/// <summary>着弾地点から見て、プレイヤーが盾の岩の影からはみ出ていないか</summary>
	/// <param name="blastOrigin">着弾地点</param>
	/// <param name="playerPos">プレイヤーの足元の位置</param>
	/// <returns>盾に守られていれば true</returns>
	bool IsShielded(const Vector3& blastOrigin, const Vector3& playerPos) const;
	/// <summary>割れていない岩に重なっていたらプレイヤーを横へ押し出す</summary>
	/// <param name="playerPos">プレイヤーの足元の位置（押し出した位置を書き戻す）</param>
	/// <returns>押し出したら true</returns>
	bool ResolvePlayerCollision(Vector3& playerPos) const;

	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">"rocks" のノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">"rocks" のノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui</summary>
	/// <param name="changed">値を変えたら true にする</param>
	void OnImGui(bool& changed);
	/// <summary>調整値を返す</summary>
	/// <returns>調整値</returns>
	Tuning& GetTuning() { return tuning_; }

private:
	enum class RockState {
		Buried,   // 地面の下で浮き始めを待つ
		Rising,   // 浮き上がり中
		Hovering, // 浮いている
		Falling,  // 落ちている（割れていない盾の岩）
		Grounded, // 地面に乗った
	};

	// 1 個の破片の動き（ワールド）
	struct FragmentState {
		Vector3 position{ 0.0f, 0.0f, 0.0f };  // 重心のワールド位置
		Vector3 velocity{ 0.0f, 0.0f, 0.0f };
		Vector3 spinAxis{ 0.0f, 1.0f, 0.0f };  // 岩の空間の回転軸
		float angle = 0.0f;
		float spinSpeed = 0.0f;
		float fadeDelay = -1.0f;               // 消え始めるまで [s]（負で消えない）
		float fadeSpeed = 0.0f;                // 1 秒あたりに減る見えている量
		bool resting = false;
	};

	struct Rock {
		Vector3 position{ 0.0f, 0.0f, 0.0f };  // 岩の原点（ワールド）
		float yaw = 0.0f;
		float spinSpeed = 0.0f;
		float buriedY = 0.0f;                  // 浮き始める前の高さ
		float hoverY = 0.0f;                   // 浮いたときの高さ
		float restY = 0.0f;                    // 地面に乗ったときの高さ
		float riseDelay = 0.0f;
		float riseTimer = 0.0f;
		float bobPhase = 0.0f;
		float fallSpeed = 0.0f;
		bool isShield = false;
		bool broken = false;
		RockState state = RockState::Buried;
		Matrix4x4 brokenWorld{};               // 割れた瞬間の岩のワールド行列（破片の基準）
		Matrix4x4 brokenWorldInverse{};
		std::vector<FragmentState> fragments;
		std::vector<FracturedMeshRenderer::FragmentPose> poses;
	};

	Matrix4x4 RockWorld(const Rock& rock) const;
	/// <summary>岩を割って破片の動きを用意する</summary>
	/// <param name="rock">割る岩</param>
	/// <param name="speed">破片が中心から離れる速さ [m/s]</param>
	/// <param name="spin">破片の回転の速さ [rad/s]</param>
	void BreakRock(Rock& rock, float speed, float spin);
	void UpdateRock(Rock& rock, float dt);
	void UpdateFragments(Rock& rock, float dt);
	/// <summary>破片の動き（ワールド）を描画の姿勢（岩の空間）へ移す</summary>
	void WritePoses(Rock& rock);

	Tuning tuning_;
	FracturedMeshRenderer renderer_;
	bool rendererReady_ = false;
	std::vector<VoronoiFracture::Fragment> fragments_;  // 割れ方（全部の岩で共通）
	float meshMinY_ = -1.0f;        // モデルの一番低い所（岩の空間）
	float meshMaxY_ = 1.0f;         // モデルの一番高い所
	float meshHorizontal_ = 1.0f;   // モデルの横幅の半径（XZ）

	std::vector<Rock> rocks_;
	std::vector<FracturedMeshRenderer::RockDraw> drawList_;
	float groundY_ = 0.0f;
	float riseDuration_ = 1.0f;
	bool gravityOn_ = false;
	bool shieldLanded_ = false;
	std::mt19937 rng_{ 1u };
};

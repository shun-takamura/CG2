#pragma once
#include "Vector3.h"
#include "StageStart/MechThrusters.h"
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

class Camera;
class DirectXCore;
class JsonValue;
class AnimatedObject3DInstance;
class EtherStrands;

/// <summary>
/// PlayerDeathPart が StagePlayScene に対して必要とする操作だけを切り出したホストインターフェース。
/// StagePlayScene が private override で実装する（RailStagePart / BossStagePart / StageStartPart と同じ方式）。
/// </summary>
class IPlayerDeathHost {
public:
	virtual ~IPlayerDeathHost() = default;
	// 羽根（PlayerWingUnit）を開いた形のまま seconds 秒で焼け落とす（シューティング）
	virtual void BurnPlayerWings(float seconds) = 0;
	// 羽根の関節のワールド位置（焼け落ちの粒子を出す所）。羽根が見えていなければ空
	virtual void CollectWingBurnPoints(std::vector<Vector3>& out) = 0;
	// 死亡演出を終えた：ゲームオーバー画面へ
	virtual void FinishPlayerDeath() = 0;
	// 自機をいまのフェーズの姿勢（クリップ）へ戻す（プレビューの終わり・途中で打ち切ったとき。ボス戦の膝つきの後始末）
	virtual void RestorePlayerPose() = 0;
};

/// <summary>
/// 自機の死亡演出（19_GameOverAndDifficulty.md §1、G1 で骨組み）。爆発はせず、機体がエーテルの色で光り、
/// 下から上へほどけて（高さの面のディゾルブ kDissolveRise）光の糸（EtherStrands）と粒になって天へ昇る。
/// 段階：前置き（Prelude。ボス戦だけ：両ひざをついてうなだれるクリップ＋スラスターの咳き込み＝G3）→ 光る（Glow）
///   → ほどける（Unravel）→ 余韻（Linger。糸と粒が昇り切る）→ 終わり（ホストへ GAMEOVER を頼む）。
/// 種類：Rail（シューティング。光り始めと同時に羽根がオレンジの縁で焼け落ち、羽根の関節から焼ける羽根の粒子を出し続ける＝§1.1）／
///   Boss（地上戦。羽根は無い。膝をつく間に 4 基のスラスターがばらばらに咳き込んで止まり、カメラは斜め前へ回り込む＝§1.2）。
/// エーテルの色は未決（§6）なので JSON の仮の値（StagePlay.json の "playerDeath" → "etherColor"）。
/// エフェクトは displayName が "Ether" で始まる部品だけ、再生のたびにエーテルの色を掛けて塗る（JSON は白で書く）。
/// 演出の間、StagePlay は操作を受け付けず・被弾させない（IsActive を見る）。
/// </summary>
class PlayerDeathPart {
public:
	enum class Kind {
		Rail,   // シューティング（§1.1）
		Boss,   // ボス戦（§1.2）
	};
	enum class State {
		Idle,
		Prelude,   // 種類ごとの前置き（G3：膝つき・スラスターの咳き込み）
		Glow,      // 機体がエーテルの色で光る
		Unravel,   // 下から上へほどけ、糸と粒が昇る
		Linger,    // 機体は消えた。糸と粒が昇り切るのを待つ
		Finished,  // ホストへ GAMEOVER を頼んだ（シーンが替わるまでこのまま）
	};

	PlayerDeathPart();
	~PlayerDeathPart();

	/// <summary>糸のメッシュを作る（StagePlay の後回しの初期化の段で呼ぶ）</summary>
	void Initialize(IPlayerDeathHost* host);
	void Finalize();

	/// <summary>
	/// 演出を始める。preview＝調整用（終わってもゲームオーバーへ行かず、自機を元に戻す）
	/// </summary>
	void Begin(Kind kind, AnimatedObject3DInstance* player, bool preview = false);
	/// <summary>途中で打ち切って自機を元に戻す（シーク・調整のやり直し）</summary>
	void Cancel();
	/// <summary>自機のスケルトンの Update の後に呼ぶ（関節の位置からほどける高さと糸の根元を決める）</summary>
	void Update(float deltaTime);
	/// <summary>糸（加算・半透明）。不透明物の後に呼ぶ（PrimitivePipeline に切り替える）</summary>
	void Draw(Camera* camera);
	/// <summary>ディゾルブの縁と糸を光らせている間</summary>
	bool HasBloom() const;
	void DrawBloom(DirectXCore* dxCore);

	bool IsActive() const { return state_ != State::Idle; }
	State GetState() const { return state_; }
	Kind GetKind() const { return kind_; }
	/// <summary>
	/// 死亡中のカメラ。Rail（G2）：レールのカメラの姿勢（camera に置いた後）から、自機へ寄り、昇る糸を追って見上げる。
	/// Boss（G3）：ボスのカメラの位置から、自機のまわりを回って斜め前・低めへ回り込み、ほどけ始めから見上げる。
	/// 毎フレーム、元のカメラの姿勢に掛け直す（積み重ならない）。重みは 0 から上がるので始まりは元のカメラのまま
	/// </summary>
	void ApplyCamera(Camera& camera) const;
	/// <summary>死亡中のスラスターの大きさ（ボス戦だけ。咳き込み→止まる）。false なら StagePlay はいつもどおり動かす</summary>
	bool GetThrusterSizes(MechThrusters::Sizes& out) const;
	/// <summary>自機の下地の発光に足す量（StagePlay の ApplyPlayerLook が足す）</summary>
	Vector3 GetBodyGlow() const;
	/// <summary>エーテルの色（仮。19 番 §6）</summary>
	const Vector3& GetEtherColor() const { return etherColor_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	void Enter(State state);
	float PreludeSeconds() const;
	// 自機の関節のワールド位置を取り直し、体の高さの範囲（余白込み）を返す。関節が無ければ false
	bool RefreshBody(float& minY, float& maxY);
	void ApplyDissolve(float planeHeight);
	void EmitStrands(float planeHeight, float deltaTime);
	// エフェクトを再生する。displayName が "Ether" で始まる部品の色にエーテルの色を掛ける
	uint64_t PlayEther(const char* effectName, const Vector3& position) const;
	void StopEffects();
	// 羽根が燃えている間、羽根の関節から焼ける羽根の粒子を出す
	void EmitFeatherBits(float deltaTime);
	void RestorePlayer();
	// ボス戦：4 基のスラスターの咳き込みの予定表を作る（基ごとにずれた、間隔の空いていく短い噴射）
	void BuildCoughSchedule();
	void ApplyRailCamera(Camera& camera, float w) const;
	void ApplyBossCamera(Camera& camera, float w) const;
	// 見上げ：ほどけ始め（Prelude＋Glow の後）から注視点を from から lookTo へ上げる高さ [m]（拡大率を掛ける前）
	float LookHeight(float from) const;

	// 咳き込みの 1 回の噴射（Prelude の始めからの秒）
	struct Puff {
		float start;
		float end;
		MechThrusters::Size size;
	};

	IPlayerDeathHost* host_ = nullptr;
	AnimatedObject3DInstance* player_ = nullptr;   // 参照のみ（シーンの dynamicAnimated_ が持つ）
	std::unique_ptr<EtherStrands> strands_;

	Kind kind_ = Kind::Rail;
	State state_ = State::Idle;
	float stateTime_ = 0.0f;
	float elapsed_ = 0.0f;       // Begin からの秒（カメラの重み）
	float wingBurnTime_ = -1.0f; // 羽根が燃え始めてからの秒（負＝燃えていない）
	float featherCarry_ = 0.0f;  // 羽根の粒子の数の端数の繰り越し
	bool preview_ = false;
	bool dissolving_ = false;
	float planeHeight_ = 0.0f;
	float strandCarry_ = 0.0f;   // 1 フレームに出す本数の端数の繰り越し

	// 関節のワールド位置（毎フレーム取り直す）と親の番号（糸の根元を骨の線分の上から選ぶ）
	std::vector<Vector3> jointPositions_;
	std::vector<int32_t> jointParents_;
	std::vector<Vector3> wingPoints_;
	std::array<std::vector<Puff>, MechThrusters::kCount> coughs_;
	std::mt19937 random_{ 20261021u };

	uint64_t ascendHandle_ = 0;
	uint64_t lightHandle_ = 0;

	// 調整値（長さは体の大きさ 1 のとき。体の拡大率を掛ける）
	Vector3 etherColor_ = { 1.0f, 0.9f, 0.65f };   // 仮（白金）。2026-10-21 時点で色は未決（19 番 §6）
	float preludeRailSeconds_ = 0.0f;
	float preludeBossSeconds_ = 2.4f;   // G3：膝つき（クリップ 2.0s）＋うなだれたまま。スラスターの咳き込みもこの間
	float glowSeconds_ = 0.6f;
	float unravelSeconds_ = 2.0f;
	float lingerSeconds_ = 1.2f;
	float glowIntensity_ = 1.2f;        // 光り切ったときの下地の発光（エーテルの色に掛ける）
	float bottomMargin_ = 0.15f;        // 関節より下（足の裏）の余白 [m]
	float topMargin_ = 0.25f;           // 関節より上（頭の上）の余白 [m]
	float edgeWidth_ = 0.08f;           // ほどける縁の幅 [m]
	float edgeIntensity_ = 3.0f;        // ほどける縁の明るさ（エーテルの色に掛ける。1 を超えてブルームに乗る）
	float noiseScale_ = 6.0f;           // 縁のゆらぎの細かさ [1/m]
	float noiseAmplitude_ = 0.06f;      // 縁のゆらぎの振れ幅 [m]
	float bloomScale_ = 1.0f;
	float strandRate_ = 160.0f;         // ほどけている間に出す糸の本数 [本/s]
	float strandBand_ = 0.08f;          // 糸の根元をほどける面からこの高さの内に置く [m]
	float strandRadius_ = 0.12f;        // 糸の根元を骨の線からこの半径の内に散らす [m]

	// 羽根の焼け落ち（G2。Glow の始めから。Linger で体を隠すより前に終える）
	float wingBurnSeconds_ = 1.6f;
	float featherBitRate_ = 30.0f;     // 焼ける羽根の粒子（PlayerFeatherBurn）を出す回数 [回/s]

	// 死亡中のカメラ（G2）。高さは自機の位置から（体の大きさ 1 のとき）
	float cameraBlendSeconds_ = 1.2f;   // レールのカメラから寄り切るまで
	float cameraApproach_ = 0.3f;       // 自機までの距離を縮める割合（0=寄らない）
	float cameraLookFrom_ = 1.2f;       // 始めの注視点の高さ（胸）[m]
	float cameraLookTo_ = 3.0f;         // 見上げ切った注視点の高さ（頭の上、糸の昇る先）[m]
	float cameraLookUpSeconds_ = 2.6f;  // ほどけ始め（Unravel）から見上げ切るまで

	// ボス戦の膝つき（G3）。クリップは rig_player_mech.py の Kneel（両ひざをつく→胸が傾く→首の力が抜ける）
	float kneelFadeSeconds_ = 0.15f;    // 直立のクリップからのつなぎ
	// スラスターの咳き込み（G3。Prelude の始めからの秒）。coughStart までは中で噴き、coughEnd で全部止まる
	float coughStart_ = 0.3f;
	float coughEnd_ = 1.55f;            // クリップで胸が前へ傾き切るころ（動力が切れて崩れる）
	float coughPuffLong_ = 0.18f;       // 始めのころの 1 回の噴射の長さ [s]
	float coughPuffShort_ = 0.05f;      // 終わりのころの 1 回の噴射の長さ [s]
	float coughGapShort_ = 0.04f;       // 始めのころの止まっている間 [s]
	float coughGapLong_ = 0.3f;         // 終わりのころの止まっている間 [s]
	float coughLargeChance_ = 0.3f;     // 噴射が大になる確率（始めのころ。終わりへ向けて 0 へ）

	// ボス戦のカメラ（G3）。自機の向きの正面から yaw 度ずれた所へ、自機のまわりを回って回り込む（高さは体の大きさ 1 のとき）
	// 2026-10-23 ユーザー：頭が下がるのを正面から見たい → 正面・低め（うなだれた顔を下から見上げる）
	float bossCamYawDeg_ = 0.0f;        // 正面からの角度（正＝自機の右手側）
	float bossCamDistance_ = 3.5f;      // 自機からの水平距離 [m]
	float bossCamHeight_ = 0.5f;        // カメラの高さ（足元から）[m]
	float bossCamOrbitSeconds_ = 1.6f;  // 回り込み切るまで
	float bossCamLookFrom_ = 0.9f;      // 始めの注視点の高さ（両ひざをついた胸）[m]
};

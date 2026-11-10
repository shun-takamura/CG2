#pragma once
#include <string>
#include <vector>
#include "Vector3.h"

class IImGuiEditable;

/// <summary>
/// BossRig がシーンに頼むこと（部位・破片の実体の出し入れ）。BossStagePart が取り次ぐ
/// </summary>
class IBossRigHost {
public:
	virtual ~IBossRigHost() = default;
	virtual IImGuiEditable* SpawnRigPiece(const std::string& prefab, const Vector3& pos) = 0;
	virtual void DestroyRigPiece(IImGuiEditable* entity) = 0;
};

/// <summary>
/// ボスの手足（プリミティブ）と部位破壊（22_ItemDrop.md §7.1）。
///   - 胴（ボス本体）の位置と向きに、腕 2 本（箱＋拳）と脚 2 本（箱）を毎フレーム付ける。
///     関節の角度はコードで振る（待機の揺れ・足踏み）。攻撃の行動は SetArmTarget で腕の構えを指示する
///   - 腕と脚は「Boss」タグの別の実体なので、今の弾・近接の当たり判定がそのまま効く。
///     部位が受けたダメージは本体にも入れる。腕は体力が尽きると壊れる（外れて落ちる）、脚は壊れない
///   - 壊れた腕は PopBrokenArm で知らせる（ひるみ・ドロップはホスト側）
/// 腕の向き：構え raise（0 = 真下に垂らす、π/2 = 前へ水平、π = 真上）と振り swing（+ で体の内側へ薙ぐ）
/// </summary>
class BossRig {
public:
	enum class Side { Left = 0, Right = 1 };

	void Initialize(IBossRigHost* host);
	/// <summary>本体のそばに手足を作る（ボス戦の開始）</summary>
	void Spawn(IImGuiEditable* body);
	/// <summary>
	/// 本体の動きに手足を付け直し、部位のダメージを本体へ回し、腕が壊れたか見る。
	/// 行動（AI）の後・死んだ実体の掃除（SweepDeadEntities）の前に呼ぶ
	/// </summary>
	void Update(float dt);
	/// <summary>本体が倒れた：残りの部位を全部外して落とす</summary>
	void BreakAll();
	/// <summary>手足・破片を全部消す（ボス戦を抜ける・やり直す）。見た目の演出はしない</summary>
	void Clear();

	// ----- 行動から使う -----
	/// <summary>このフレームの腕の構え。呼ばれなかった腕は待機の構えへ戻る</summary>
	void SetArmTarget(Side side, float raise, float swing, float smoothTime = 0.06f);
	bool IsArmAlive(Side side) const;
	bool HasAnyArm() const { return IsArmAlive(Side::Left) || IsArmAlive(Side::Right); }
	/// <summary>拳の中心（ワールド）。腕が無ければ本体の中心</summary>
	Vector3 GetFistPos(Side side) const;

	// ----- 部位破壊の知らせ -----
	/// <summary>このフレームに壊れた腕があれば 1 本取り出す（外れた所の位置）</summary>
	bool PopBrokenArm(Side& outSide, Vector3& outPos);

	void OnImGui();

private:
	struct Arm {
		IImGuiEditable* arm = nullptr;  // 当たり判定・体力を持つ（boss_arm）
		IImGuiEditable* fist = nullptr; // 見た目だけ（boss_fist）
		int   lastHp = 0;
		bool  broken = false;
		float raise = 0.3f, swing = 0.0f;           // 今の構え
		float targetRaise = 0.3f, targetSwing = 0.0f;
		float smoothTime = 0.15f;
		bool  targetSet = false;
	};
	struct Leg {
		IImGuiEditable* leg = nullptr; // 当たり判定あり・壊れない（受けた分は本体へ）
		int lastHp = 0;
	};
	struct Debris {
		IImGuiEditable* entity = nullptr;
		Vector3 velocity{};
		float spin = 0.0f;
		float spinAngle = 0.0f;
		float yaw = 0.0f;
		float life = 0.0f;
	};

	// 胴の座標系（右・上・前）で表した点・向きをワールドへ
	Vector3 ToWorldPoint(const Vector3& local) const;
	Vector3 ToWorldDir(const Vector3& local) const;
	Vector3 ArmDirLocal(Side side, float raise, float swing) const;
	Vector3 ShoulderLocal(Side side) const;
	// 箱・円柱（ローカル +Y が長さの向き）を dir へ向ける回転（PrimitiveMesh・当たり判定と同じ Rz·Ry·Rx）
	static Vector3 RotateYAxisTo(const Vector3& dir);
	// 部位が受けたダメージを本体へ。返り値は今の体力
	int ForwardDamage(IImGuiEditable* part, int& lastHp, bool keepAlive);
	void BreakArm(Side side);
	void SpawnDebris(const std::string& prefab, const Vector3& pos, const Vector3& rotate, const Vector3& kick);
	void UpdateDebris(float dt);

	IBossRigHost* host_ = nullptr;
	IImGuiEditable* body_ = nullptr;
	Arm arms_[2];
	Leg legs_[2];
	std::vector<Debris> debris_;
	struct Broken { Side side; Vector3 pos; };
	std::vector<Broken> brokenQueue_;

	// 胴の向き・歩き
	Vector3 bodyPos_{};
	Vector3 prevBodyPos_{};
	float bodyYaw_ = 0.0f;
	bool  hasPrevBody_ = false;
	float walkPhase_ = 0.0f;
	float walkAmount_ = 0.0f; // 0..1（動いている間だけ足踏み）
	float time_ = 0.0f;
	float groundY_ = 0.0f;    // 胴の中心 − bodyHeight_（スポーン時に決める）

	// ----- 形（胴の中心から。m）-----
	float bodyHeight_     = 3.4f;  // 胴の中心の接地高さ（BossStagePart の bossHeight_ と同じ）
	float shoulderX_      = 2.2f;
	float shoulderY_      = 0.5f;
	float upperArmLength_ = 3.4f;  // 箱の長さ（boss_arm の scale.y と合わせる）
	float fistOffset_     = 4.0f;  // 肩から拳の中心まで
	float hipX_           = 1.0f;
	float hipY_           = -1.0f;
	float legLength_      = 2.4f;  // 腰から足元（boss_leg の scale.y と合わせる）
	float armSplay_       = 0.3f;  // 待機で腕を外へ開く量
	// ----- 動き -----
	float idleRaise_      = 0.6f;  // 拳が床に付くくらい（下げると床にめり込む）
	float idleSwayAmp_    = 0.08f;
	float strideLength_   = 2.5f;  // 足踏み 1 周で進む距離 [m]
	float walkSwingAmp_   = 0.45f; // 脚の振り [rad]
	float damageToBody_   = 1.0f;  // 部位が受けたダメージを本体へ入れる割合
	// ----- 破片 -----
	float debrisGravity_  = 25.0f;
	float debrisLife_     = 4.0f;
};

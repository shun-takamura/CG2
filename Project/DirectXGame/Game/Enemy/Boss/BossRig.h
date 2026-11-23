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
	/// <summary>部位・破片の実体をシーンに出す</summary>
	/// <param name="prefab">出すプレハブ名</param>
	/// <param name="pos">出す位置（ワールド）</param>
	/// <returns>出した実体。出せなければ nullptr</returns>
	virtual IImGuiEditable* SpawnRigPiece(const std::string& prefab, const Vector3& pos) = 0;
	/// <summary>部位・破片の実体をシーンから消す</summary>
	/// <param name="entity">消す実体</param>
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

	/// <summary>部位・破片の出し入れを頼む相手を受け取る</summary>
	/// <param name="host">実体の出し入れを取り次ぐホスト（BossStagePart）</param>
	void Initialize(IBossRigHost* host);
	/// <summary>本体のそばに手足を作る（ボス戦の開始）</summary>
	/// <param name="body">手足を付ける本体（胴）の実体</param>
	void Spawn(IImGuiEditable* body);
	/// <summary>
	/// 本体の動きに手足を付け直し、部位のダメージを本体へ回し、腕が壊れたか見る。
	/// 行動（AI）の後・死んだ実体の掃除（SweepDeadEntities）の前に呼ぶ
	/// </summary>
	/// <param name="dt">経過時間 [秒]</param>
	void Update(float dt);
	/// <summary>本体が倒れた：残りの部位を全部外して落とす</summary>
	void BreakAll();
	/// <summary>手足・破片を全部消す（ボス戦を抜ける・やり直す）。見た目の演出はしない</summary>
	void Clear();

	// ----- 行動から使う -----
	/// <summary>このフレームの腕の構えを指示する。呼ばれなかった腕は待機の構えへ戻る</summary>
	/// <param name="side">左右どちらの腕か</param>
	/// <param name="raise">構え [ラジアン]（0 = 真下に垂らす、π/2 = 前へ水平、π = 真上）</param>
	/// <param name="swing">振り [ラジアン]（+ で体の内側へ薙ぐ）</param>
	/// <param name="smoothTime">目標の構えへ寄る時間 [秒]</param>
	void SetArmTarget(Side side, float raise, float swing, float smoothTime = 0.06f);
	/// <summary>腕が残っているか</summary>
	/// <param name="side">左右どちらの腕か</param>
	/// <returns>腕があって壊れていなければ true</returns>
	bool IsArmAlive(Side side) const;
	/// <summary>腕が 1 本でも残っているか</summary>
	/// <returns>どちらかの腕が残っていれば true</returns>
	bool HasAnyArm() const { return IsArmAlive(Side::Left) || IsArmAlive(Side::Right); }
	/// <summary>拳の中心のワールド位置を求める</summary>
	/// <param name="side">左右どちらの腕か</param>
	/// <returns>拳の中心（ワールド）。腕が無い・壊れていれば本体の中心</returns>
	Vector3 GetFistPos(Side side) const;

	// ----- 部位破壊の知らせ -----
	/// <summary>壊れた腕の知らせを 1 本分取り出す（古い順）</summary>
	/// <param name="outSide">壊れた腕の左右</param>
	/// <param name="outPos">外れた所（拳の中心）のワールド位置</param>
	/// <returns>取り出せたら true。知らせが無ければ false（out は書かない）</returns>
	bool PopBrokenArm(Side& outSide, Vector3& outPos);

	/// <summary>形・動き・破片の調整値と部位の状態を ImGui に出す</summary>
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

	/// <summary>胴の座標系（右・上・前）で表した点をワールドへ移す</summary>
	/// <param name="local">胴の中心からのローカル位置 [m]</param>
	/// <returns>ワールド位置</returns>
	Vector3 ToWorldPoint(const Vector3& local) const;
	/// <summary>胴の座標系（右・上・前）で表した向きをワールドへ回す（平行移動なし）</summary>
	/// <param name="local">ローカルの向き</param>
	/// <returns>ワールドの向き</returns>
	Vector3 ToWorldDir(const Vector3& local) const;
	/// <summary>構えと振りから、肩から拳への単位方向を胴の座標系で求める（待機の外への開きも含む）</summary>
	/// <param name="side">左右どちらの腕か</param>
	/// <param name="raise">構え [ラジアン]</param>
	/// <param name="swing">振り [ラジアン]</param>
	/// <returns>胴の座標系での単位方向</returns>
	Vector3 ArmDirLocal(Side side, float raise, float swing) const;
	/// <summary>肩の位置を胴の座標系で求める</summary>
	/// <param name="side">左右どちらの肩か</param>
	/// <returns>胴の中心からのローカル位置 [m]</returns>
	Vector3 ShoulderLocal(Side side) const;
	/// <summary>箱・円柱（ローカル +Y が長さの向き）を dir へ向けるオイラー角を求める（PrimitiveMesh・当たり判定と同じ Rz·Ry·Rx）</summary>
	/// <param name="dir">向けたい単位方向（ワールド）</param>
	/// <returns>オイラー角 [ラジアン]</returns>
	static Vector3 RotateYAxisTo(const Vector3& dir);
	/// <summary>前回から部位が受けたダメージを本体へ入れる</summary>
	/// <param name="part">部位の実体</param>
	/// <param name="lastHp">前回の部位の体力（読んで、今の体力へ書き換える）</param>
	/// <param name="keepAlive">true なら部位の体力を満タンへ戻す（壊れない脚）</param>
	/// <returns>部位の今の体力</returns>
	int ForwardDamage(IImGuiEditable* part, int& lastHp, bool keepAlive);
	/// <summary>腕を壊す：エフェクトと音を出し、腕と拳を破片に置き換え、壊れた知らせを積む。もう壊れていれば何もしない</summary>
	/// <param name="side">左右どちらの腕か</param>
	void BreakArm(Side side);
	/// <summary>弾かれて落ちる見た目だけの破片を出す</summary>
	/// <param name="prefab">破片のプレハブ名</param>
	/// <param name="pos">出す位置（ワールド）</param>
	/// <param name="rotate">出す時の向き（オイラー角 [ラジアン]）</param>
	/// <param name="kick">初速 [m/秒]</param>
	void SpawnDebris(const std::string& prefab, const Vector3& pos, const Vector3& rotate, const Vector3& kick);
	/// <summary>破片を重力で落として回し、足場で止め、寿命が尽きたら消す</summary>
	/// <param name="dt">経過時間 [秒]</param>
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

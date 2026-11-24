#pragma once
#include "IPlayerBeamHost.h"
#include "Vector3.h"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>
/// ビームの種類ごとの値（Resources/Json/Tuning/PlayerBeam.json）。見た目はエフェクト（EffectDef の Ribbon の Beam）で作る
/// </summary>
struct PlayerBeamProfile {
	std::string effect;           // 撃っている間に出すエフェクト名
	std::string hitEffect;        // 当たった所に出すエフェクト名
	float duration     = 0.5f;    // 撃ち続ける時間 [秒]
	float length       = 300.0f;  // 長さ [m]
	float radius       = 1.2f;    // 判定の太さ（半径）[m]
	float extendTime   = 0.05f;   // 判定が先まで伸びる時間 [秒]（エフェクトの Extend Time に合わせる）
	float tickInterval = 0.05f;   // 同じ敵にダメージを入れる間隔 [秒]
	float damageScale  = 1.0f;    // 1 回のダメージ＝撃ったときの攻撃力 × これ
};

/// <summary>
/// 自機のビーム（チャージ射撃・攻撃系のファンネル大）。撃っている間は持ち主が毎フレーム SetPose で根元と向きを渡し、
/// 線分の上の敵すべてに tickInterval ごとにダメージを入れる（貫通する）。見た目はエフェクトに任せる
/// （位置・向き・長さを毎フレーム渡し、終わったら Release で細くなって消える）。
/// </summary>
class PlayerBeamPart {
public:
	using BeamId = uint32_t;
	static constexpr BeamId kInvalidBeam = 0;

	/// <summary>ビームの種類</summary>
	enum class Kind {
		Charge1,     // チャージ 1 段目
		Charge2,     // チャージ 2 段目
		FunnelLarge, // 攻撃系のファンネル大
		Count,
	};

	/// <summary>シーンへの窓口を受け取り、調整値を読む</summary>
	/// <param name="host">シーンへの窓口</param>
	void Initialize(IPlayerBeamHost* host);
	/// <summary>撃ち始める</summary>
	/// <param name="kind">ビームの種類</param>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド。正規化済み）</param>
	/// <param name="attackPower">撃ったときの攻撃力（1 回のダメージ＝これ × damageScale）</param>
	/// <param name="radiusScale">判定の太さに掛ける倍率（メイン武器の弾の大きさなど）</param>
	/// <returns>ビームの番号（SetPose / IsActive に使う）</returns>
	BeamId Fire(Kind kind, const Vector3& from, const Vector3& dir, int attackPower, float radiusScale = 1.0f);
	/// <summary>根元と向きを付け直す（撃っている間、持ち主が毎フレーム呼ぶ）</summary>
	/// <param name="id">ビームの番号</param>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド。正規化済み）</param>
	void SetPose(BeamId id, const Vector3& from, const Vector3& dir);
	/// <summary>撃っている途中かを返す</summary>
	/// <param name="id">ビームの番号</param>
	/// <returns>途中なら true</returns>
	bool IsActive(BeamId id) const;
	/// <summary>撃つのをやめる（エフェクトは細くなって消える）</summary>
	/// <param name="id">ビームの番号</param>
	void Stop(BeamId id);
	/// <summary>全部やめる（シーンの終わり・死亡演出の始まり）</summary>
	void StopAll();
	/// <summary>時間を進め、当たり判定とエフェクトの位置を更新する</summary>
	/// <param name="dt">経過時間 [秒]（自機の弾と同じ時間）</param>
	void Update(float dt);

	/// <summary>種類ごとの値を返す</summary>
	/// <param name="kind">ビームの種類</param>
	/// <returns>値</returns>
	const PlayerBeamProfile& GetProfile(Kind kind) const { return profiles_[static_cast<size_t>(kind)]; }

	/// <summary>調整値を JSON から読む</summary>
	/// <returns>読めたら true（ファイルが無ければ既定値のまま false）</returns>
	bool LoadTuning();
	/// <summary>調整値を JSON へ書く</summary>
	void SaveTuning() const;
	/// <summary>調整値の ImGui（Debug だけ）</summary>
	void OnImGui();

private:
	struct Beam {
		BeamId id = kInvalidBeam;
		Kind kind = Kind::Charge1;
		Vector3 from{ 0.0f, 0.0f, 0.0f };
		Vector3 dir{ 0.0f, 0.0f, 1.0f };
		float timer = 0.0f;
		int damage = 0;
		float radiusScale = 1.0f;
		uint64_t effectHandle = 0;
		bool ending = false;
		// ダメージの持ち主ごとの次に入れられるまでの時間（キーは比べるだけで中身を読まない）
		std::unordered_map<IImGuiEditable*, float> cooldowns;
	};

	/// <summary>ビームを番号から探す</summary>
	/// <param name="id">ビームの番号</param>
	/// <returns>ビーム。無ければ nullptr</returns>
	Beam* Find(BeamId id);
	/// <summary>エフェクトの位置・向き・長さを今のビームに合わせる</summary>
	/// <param name="b">ビーム</param>
	/// <param name="length">今の長さ [m]</param>
	void ApplyEffectPose(const Beam& b, float length) const;

	IPlayerBeamHost* host_ = nullptr;
	std::array<PlayerBeamProfile, static_cast<size_t>(Kind::Count)> profiles_{};
	std::vector<Beam> beams_;
	std::vector<PlayerBeamHit> hitsScratch_;
	BeamId nextId_ = 1;
};

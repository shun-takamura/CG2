#pragma once
#include "Vector3.h"
#include <array>
#include <cstdint>
#include <string>

class AnimatedObject3DInstance;
class JsonValue;

/// <summary>
/// 自機の 4 基のスラスターの噴射（16_StageStart.md §3.2、F5）。2026-10-19 ユーザー決定：
///   背中 2 基・ふくらはぎ 2 基（翼は機械ではないので付けない）。エフェクトは大・中・小の 3 つ
///   （Resources/Json/Effects/Thruster_S / M / L.json。動きを確かめてから EffectEditor でユーザーが直す）。
///   射出の SPACE では出力に合わせて小 → 中 → 大。
///   移動はポイント制（2026-10-19 ユーザー：斜めの入力を組み合わせるため）：ニュートラルは全基 1（中）、
///   W は背中 2 基 +1、S は全基 −1、A は右 2 基 +1・左 2 基 −1、D は左 2 基 +1・右 2 基 −1、0（小）〜 2（大）でクランプ。
/// 各基は機体のボーンの位置＋ずらし（ワールドの向き・m。機体は +Z を向いて回さない）に置き、噴く向きはオイラー角（度）で持つ。
/// エフェクトのローカルは噴き出し口が原点・噴射が -Y。大きさが変わったら今のを止めて別のを再生する。
/// 調整値は StagePlay.json の "stageStart" → "thrusters"。
/// </summary>
class MechThrusters {
public:
	enum class Size { Off, Small, Medium, Large };
	enum Index { kBackLeft, kBackRight, kCalfLeft, kCalfRight, kCount };
	using Sizes = std::array<Size, kCount>;

	MechThrusters();
	~MechThrusters();

	/// <summary>全部止める（演出の終わり・一人称に切り替えたとき）</summary>
	void StopAll();
	/// <summary>sizes＝4 基それぞれの大きさ。mech のボーンから置く（mech が無ければ止める）</summary>
	void Update(const AnimatedObject3DInstance* mech, const Sizes& sizes);

	/// <summary>移動の入力から大きさを決める（機体確認・ゲーム中の移動。ポイント制、上のコメント）</summary>
	Sizes FromMove(bool up, bool left, bool down, bool right) const;
	/// <summary>出力 0..1 から全部同じ大きさ（射出の SPACE。0 でも小から）</summary>
	Sizes FromThrust(float thrust) const;
	static Sizes All(Size size) { return { size, size, size, size }; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	struct Mount {
		std::string bone;
		Vector3 offset;              // ボーンの位置からのずらし [m]
		Vector3 rotationDegrees;     // 噴く向き（-Y をこの角度で回す）
	};
	const std::string& EffectName(Size size) const;

	std::array<Mount, kCount> mounts_;
	std::array<uint64_t, kCount> handles_{};
	std::array<Size, kCount> playing_{};

	// 調整値
	std::string smallEffect_ = "Thruster_S";
	std::string mediumEffect_ = "Thruster_M";
	std::string largeEffect_ = "Thruster_L";
	float smallBelow_ = 0.34f;    // 射出の出力がこれ未満で小
	float mediumBelow_ = 0.67f;   // これ未満で中、以上で大
	int neutralLevel_ = 1;        // 移動のニュートラルのポイント（0 小 / 1 中 / 2 大）
};

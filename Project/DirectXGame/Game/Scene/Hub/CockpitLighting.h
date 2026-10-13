#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class HubMechModel;
class JsonValue;

/// <summary>
/// コクピットの中の見た目（18_HubTransitions.md ①「C で決まったこと」）。部位のマテリアルと緑のポイントライトを毎フレーム決める。
///  - 灯り：緑のポイントライト（LightManager から借りる）と、光る部位（CP_Glow など）の発光。強さ level 0..1
///  - 外を映す：モニターの部位（壁・ハッチ）を画面座標の六角形のディゾルブで中心から晴らす。reveal 0..1
/// 部位はマテリアルの名前（.mat のパスに含まれる文字）で選ぶ。ModelInstance はシーンをまたいで共有されるので、
/// 毎フレーム書き直し、Finalize で発光とディゾルブを 0 に戻す。調整値は Hub.json の "cockpitLighting"。
/// </summary>
class CockpitLighting {
public:
	CockpitLighting();
	void ResetToDefaults();

	void Initialize();
	void Finalize(HubMechModel* mech);

	/// <summary>
	/// 灯りの強さと外が透ける割合をマテリアル・ライトへ反映する（screenSize は六角形の中心と大きさに使う）。
	/// signColor はハッチの表示板の色。followSign の部位と緑のポイントライトはこの色になる（LOCKED＝緑 / UNLOCK＝赤とピンク）
	/// </summary>
	void Apply(HubMechModel& mech, float level, float reveal, const Vector2& screenSize, const Vector3& signColor);

	/// <summary>発光 RT に描く物があるか（灯りが点いている・六角形が光っている）</summary>
	bool HasBloom() const { return level_ > 0.0f || IsRevealing(); }
	/// <summary>六角形の発光帯が出ている（発光パスで発光していない部位も描く）</summary>
	bool IsRevealing() const { return reveal_ > 0.0f && reveal_ < 1.0f; }
	float GetBloomScale() const { return bloomScale_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	struct GlowPart {
		std::string material;   // .mat のパスに含まれる文字（例 "CP_Glow"）
		Vector3 color;          // followSign が false のときの色
		float strength = 1.0f;  // level = 1 のときの発光の強さ
		bool followSign = true; // 表示板の色にする
	};
	struct Lamp {
		Vector3 offset;         // コクピットの軸（HubMechModel::GetCockpitAxis）からの位置
		float intensity = 1.0f;
		float radius = 2.0f;
		float decay = 2.0f;
	};
	static constexpr uint32_t kLampCount = 2;

	float level_ = 0.0f;
	float reveal_ = 0.0f;
	std::array<uint32_t, kLampCount> lampSlots_{};

	// 調整値
	std::vector<GlowPart> glowParts_;
	std::vector<std::string> monitorParts_;   // 外を映す（六角形で晴れる）部位
	std::array<Lamp, kLampCount> lamps_{};
	Vector3 lampColor_ = { 0.3f, 1.0f, 0.55f };   // lampFollowSign が false のときの色
	bool lampFollowSign_ = true;                  // ポイントライトも表示板の色にする
	float hexCellPixels_ = 42.0f;   // 六角形の大きさ（画面の高さ 900 のとき）
	float hexBand_ = 0.12f;         // 光る帯の幅（中心からの遠さ 0..1 の単位）
	float hexJitter_ = 0.12f;       // マスごとのばらつき
	Vector3 hexColor_ = { 0.35f, 1.0f, 0.85f };
	float bloomScale_ = 3.0f;       // 発光 RT に書くときの倍率
};

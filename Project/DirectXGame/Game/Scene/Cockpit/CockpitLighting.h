#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class CockpitModel;
class JsonValue;

/// <summary>
/// コクピットの中の見た目（18_HubTransitions.md ①「C で決まったこと」）。部位のマテリアルと緑のポイントライトを毎フレーム決める。
///  - 灯り：緑のポイントライト（LightManager から借りる）と、光る部位（CP_Glow など）の発光。強さ level 0..1
///  - 外を映す：モニターの部位（壁・ハッチ）を画面座標の六角形のディゾルブで中心から晴らす。reveal 0..1
/// 部位はマテリアルの名前（.mat のパスに含まれる文字）で選ぶ。ModelInstance はシーンをまたいで共有されるので、
/// 毎フレーム書き直し、Finalize で発光とディゾルブを 0 に戻す。調整値は Hub.json の "cockpitLighting"
/// （StagePlay のスタート演出も同じ値を読む。③の切れ目で灯りの見え方をそろえるため。16_StageStart.md §4.1）。
/// </summary>
class CockpitLighting {
public:
	/// <summary>ポイントライトの枠を空にし、調整値を既定にする</summary>
	CockpitLighting();
	/// <summary>調整値（光る部位・外を映す部位・ポイントライト・六角形）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>緑のポイントライトを LightManager から借り、灯りと外の透け具合を 0 に戻す</summary>
	void Initialize();
	/// <summary>借りたポイントライトを返し、共有のマテリアルの発光とディゾルブを 0 に戻す（他のシーンへ持ち越さない）</summary>
	/// <param name="cockpit">戻すコクピット。nullptr ならライトだけ返す</param>
	void Finalize(CockpitModel* cockpit);

	/// <summary>
	/// 灯りの強さと外が透ける割合をマテリアル・ライトへ反映する。
	/// followSign の部位と緑のポイントライトは signColor になる（LOCKED＝緑 / UNLOCK＝赤とピンク）
	/// </summary>
	/// <param name="cockpit">反映するコクピット</param>
	/// <param name="level">灯りの強さ 0〜1</param>
	/// <param name="reveal">外が透ける割合 0〜1（六角形のディゾルブで中心から晴れる）</param>
	/// <param name="screenSize">画面の大きさ [px]（六角形の中心と大きさに使う）</param>
	/// <param name="signColor">ハッチの表示板の色（RGB 0〜1）</param>
	void Apply(CockpitModel& cockpit, float level, float reveal, const Vector2& screenSize, const Vector3& signColor);

	/// <summary>発光 RT に描く物があるかを返す</summary>
	/// <returns>灯りが点いているか、六角形が光っていれば true</returns>
	bool HasBloom() const { return level_ > 0.0f || IsRevealing(); }
	/// <summary>六角形の発光帯が出ているかを返す（発光パスで発光していない部位も描く）</summary>
	/// <returns>外が透ける途中（0 と 1 の間）なら true</returns>
	bool IsRevealing() const { return reveal_ > 0.0f && reveal_ < 1.0f; }
	/// <summary>発光 RT に書くときの倍率を返す</summary>
	/// <returns>倍率</returns>
	float GetBloomScale() const { return bloomScale_; }

	/// <summary>調整値を JSON（Hub.json の "cockpitLighting"）から読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	struct GlowPart {
		std::string material;   // .mat のパスに含まれる文字（例 "CP_Glow"）
		Vector3 color;          // followSign が false のときの色
		float strength = 1.0f;  // level = 1 のときの発光の強さ
		bool followSign = true; // 表示板の色にする
	};
	struct Lamp {
		Vector3 offset;         // コクピットの軸（CockpitModel::GetAxis）からの位置
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

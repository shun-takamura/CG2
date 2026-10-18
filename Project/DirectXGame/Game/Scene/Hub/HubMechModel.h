#pragma once
#include "Vector3.h"
#include <memory>

class Object3DInstance;
class Object3DManager;
class CockpitModel;
class DirectXCore;
class JsonValue;

/// <summary>
/// HUB 用の機体（17_HubScene.md §5.1）。本体と、コクピット・上のハッチ（CockpitModel）をまとめて置く。
/// 原点＝足元の中央、正面はエンジンの +Z。ハッチはコクピットの軸（X 軸に平行）まわりに回る。
/// HUB（乗り込み・降りる）とステージ選択（コクピットの内側）で使い回す。
/// コクピットだけは StagePlay のスタート演出でも使うので Cockpit/CockpitModel に分けてある（16_StageStart.md §4.1）。
/// </summary>
class HubMechModel {
public:
	HubMechModel();
	~HubMechModel();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore);
	void Update();
	void Draw(DirectXCore* dxCore);
	/// <summary>
	/// 発光パス：コクピットとハッチの「光る分」を発光 RT に描く。
	/// includeAll なら発光していない部位も描く（六角形のディゾルブの発光帯を光らせる間）
	/// </summary>
	void DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll);

	// 神殿の鏡の床に映すため（HubTemple）
	Object3DInstance* GetBody() const { return body_.get(); }
	// コクピットの中の見た目（灯り・発光・六角形）を部位のマテリアルで変えるため（CockpitLighting）
	CockpitModel& GetCockpitModel() const { return *cockpit_; }
	Object3DInstance* GetCockpit() const;
	Object3DInstance* GetHatch() const;

	/// <summary>ハッチの開き具合（1 = 開いて待っている、0 = 閉じ切り）</summary>
	void SetHatchOpenRatio(float ratio);
	float GetHatchOpenRatio() const;

	/// <summary>
	/// 本体を描かない（コクピットとハッチは描く）。コクピットの中にいる間は全天周のモニターに自分の機体が映り込まないよう隠す。
	/// StagePlay の一人称も本体を描かないので、③ の切れ目の前後で見え方がそろう
	/// </summary>
	void SetBodyHidden(bool hidden) { bodyHidden_ = hidden; }
	bool IsBodyHidden() const { return bodyHidden_; }

	/// <summary>機体の原点（足元の中央）。乗り込みのカメラのキーはこの原点からの相対で持つ</summary>
	const Vector3& GetPosition() const { return position_; }
	/// <summary>③ のリフトの降下で機体ごと下げる（16_StageStart.md）。反映は Update</summary>
	void SetPosition(const Vector3& position) { position_ = position; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	void ApplyBodyMaterial();

	std::unique_ptr<Object3DInstance> body_;
	std::unique_ptr<CockpitModel> cockpit_;

	Vector3 position_ = { 0.0f, 0.0f, 0.0f };
	bool bodyHidden_ = false;

	// 調整値（Hub.json の "mech"。ハッチの開いた角度 hatchOpenDegrees は CockpitModel が持つ）
	bool drawBody_ = true;   // デバッグ：本体を隠してコクピットとハッチだけ見る
	// 本体の質感の上書き（cook の値は metallicRoughness テクスチャの中央値で、粗さ 0.671 とかなりマット）。
	// エンジンは金属度・粗さのテクスチャを読まないので部位ごとの差は出せない
	bool overrideBodyMaterial_ = true;
	float bodyRoughness_ = 0.35f;
	float bodyMetallic_ = 0.05f;
};

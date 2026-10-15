#pragma once
#include "Vector3.h"
#include <memory>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class JsonValue;

/// <summary>
/// HUB 用の機体（17_HubScene.md §5.1）。本体・コクピット・上のハッチの 3 つのモデルをまとめて置く。
/// 原点＝足元の中央、正面はエンジンの +Z。ハッチはコクピットの軸（X 軸に平行）まわりに回る。
/// HUB（乗り込み・降りる）とステージ選択（コクピットの内側）で使い回す。
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
	Object3DInstance* GetCockpit() const { return cockpit_.get(); }
	Object3DInstance* GetHatch() const { return hatch_.get(); }

	/// <summary>ハッチの開き具合（1 = 開いて待っている、0 = 閉じ切り）</summary>
	void SetHatchOpenRatio(float ratio) { hatchOpenRatio_ = ratio; }
	float GetHatchOpenRatio() const { return hatchOpenRatio_; }

	/// <summary>機体の原点（足元の中央）。乗り込みのカメラのキーはこの原点からの相対で持つ</summary>
	const Vector3& GetPosition() const { return position_; }
	/// <summary>③ のリフトの降下で機体ごと下げる（16_StageStart.md）。反映は Update</summary>
	void SetPosition(const Vector3& position) { position_ = position; }

	// 機体の原点からの位置（エンジン座標。17 番 §5.1 の表）
	static Vector3 GetCockpitAxis();      // コクピットの円柱の軸＝ハッチの回転の軸
	static Vector3 GetSeatedEye();        // 座ったパイロットの目

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	void ApplyBodyMaterial();

	std::unique_ptr<Object3DInstance> body_;
	std::unique_ptr<Object3DInstance> cockpit_;
	std::unique_ptr<Object3DInstance> hatch_;

	Vector3 position_ = { 0.0f, 0.0f, 0.0f };
	float hatchOpenRatio_ = 1.0f;

	// 調整値（Hub.json の "mech"）
	// 開いた角度（X 軸まわり、度）。Blender の −115° はエンジンでも同じ符号（どちらも +Z の前が上・後ろへ回る）
	float hatchOpenDegrees_ = -115.0f;
	bool drawBody_ = true;   // デバッグ：本体を隠してコクピットとハッチだけ見る
	// 本体の質感の上書き（cook の値は metallicRoughness テクスチャの中央値で、粗さ 0.671 とかなりマット）。
	// エンジンは金属度・粗さのテクスチャを読まないので部位ごとの差は出せない
	bool overrideBodyMaterial_ = true;
	float bodyRoughness_ = 0.35f;
	float bodyMetallic_ = 0.05f;
};

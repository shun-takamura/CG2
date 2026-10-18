#pragma once
#include "Vector3.h"
#include <memory>

class Object3DInstance;
class Object3DManager;
class DirectXCore;

/// <summary>
/// 機体のコクピットの内装（横に倒した円柱・座席・操縦桿・操作盤）と上のハッチ（17_HubScene.md §5.1）。
/// HUB（HubMechModel が本体と一緒に持つ）と StagePlay のスタート演出（一人称のときは機体を描かず、これだけを描く。
/// 16_StageStart.md §4.1）の両方で使う。原点＝機体の足元の中央、正面はエンジンの +Z。
/// ハッチはコクピットの軸（X 軸に平行）まわりに回る。
/// </summary>
class CockpitModel {
public:
	CockpitModel();
	~CockpitModel();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore);
	void Update();
	void Draw(DirectXCore* dxCore);
	/// <summary>
	/// 発光パス：コクピットとハッチの「光る分」を発光 RT に描く。
	/// includeAll なら発光していない部位も描く（六角形のディゾルブの発光帯を光らせる間）
	/// </summary>
	void DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll);

	// 部位のマテリアルで見た目（灯り・発光・六角形）を変えるため（CockpitLighting）・鏡の床に映すため（HubTemple）
	Object3DInstance* GetCockpit() const { return cockpit_.get(); }
	Object3DInstance* GetHatch() const { return hatch_.get(); }

	/// <summary>機体の原点（足元の中央）。反映は Update</summary>
	const Vector3& GetPosition() const { return position_; }
	void SetPosition(const Vector3& position) { position_ = position; }

	/// <summary>ハッチの開き具合（1 = 開いて待っている、0 = 閉じ切り）</summary>
	void SetHatchOpenRatio(float ratio) { hatchOpenRatio_ = ratio; }
	float GetHatchOpenRatio() const { return hatchOpenRatio_; }
	/// <summary>開いた角度（X 軸まわり、度）</summary>
	void SetHatchOpenDegrees(float degrees) { hatchOpenDegrees_ = degrees; }
	float GetHatchOpenDegrees() const { return hatchOpenDegrees_; }

	// 機体の原点からの位置（エンジン座標。17 番 §5.1 の表）
	static Vector3 GetAxis();         // コクピットの円柱の軸＝ハッチの回転の軸
	static Vector3 GetSeatedEye();    // 座ったパイロットの目

private:
	std::unique_ptr<Object3DInstance> cockpit_;
	std::unique_ptr<Object3DInstance> hatch_;

	Vector3 position_ = { 0.0f, 0.0f, 0.0f };
	float hatchOpenRatio_ = 1.0f;
	// Blender の −115° はエンジンでも同じ符号（どちらも +Z の前が上・後ろへ回る）
	float hatchOpenDegrees_ = -115.0f;
};

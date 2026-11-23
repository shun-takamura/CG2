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

	/// <summary>コクピットとハッチのモデルを読み込み、今の位置・開き具合を反映する</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore);
	/// <summary>位置とハッチの開き具合（軸まわりの回転）を反映して行列を更新する</summary>
	void Update();
	/// <summary>コクピットとハッチを描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パス：コクピットとハッチの「光る分」を発光 RT に描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="bloomScale">発光 RT に書くときの倍率</param>
	/// <param name="includeAll">true なら発光していない部位も描く（六角形のディゾルブの発光帯を光らせる間）</param>
	void DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll);

	/// <summary>コクピットの内装を返す（部位のマテリアルで見た目を変える CockpitLighting・鏡の床に映す HubTemple が使う）</summary>
	/// <returns>コクピットの内装。Initialize 前は nullptr</returns>
	Object3DInstance* GetCockpit() const { return cockpit_.get(); }
	/// <summary>ハッチを返す（CockpitLighting・HubTemple が使う）</summary>
	/// <returns>ハッチ。Initialize 前は nullptr</returns>
	Object3DInstance* GetHatch() const { return hatch_.get(); }

	/// <summary>機体の原点（足元の中央）を返す</summary>
	/// <returns>ワールド座標</returns>
	const Vector3& GetPosition() const { return position_; }
	/// <summary>機体の原点（足元の中央）を設定する。反映は Update</summary>
	/// <param name="position">ワールド座標</param>
	void SetPosition(const Vector3& position) { position_ = position; }

	/// <summary>ハッチの開き具合を設定する</summary>
	/// <param name="ratio">1 = 開いて待っている、0 = 閉じ切り</param>
	void SetHatchOpenRatio(float ratio) { hatchOpenRatio_ = ratio; }
	/// <summary>ハッチの開き具合を返す</summary>
	/// <returns>1 = 開いて待っている、0 = 閉じ切り</returns>
	float GetHatchOpenRatio() const { return hatchOpenRatio_; }
	/// <summary>開き切ったときの角度を設定する</summary>
	/// <param name="degrees">X 軸まわりの角度 [度]</param>
	void SetHatchOpenDegrees(float degrees) { hatchOpenDegrees_ = degrees; }
	/// <summary>開き切ったときの角度を返す</summary>
	/// <returns>X 軸まわりの角度 [度]</returns>
	float GetHatchOpenDegrees() const { return hatchOpenDegrees_; }

	/// <summary>コクピットの円柱の軸＝ハッチの回転の軸を返す</summary>
	/// <returns>機体の原点からの位置（エンジン座標。17 番 §5.1 の表）</returns>
	static Vector3 GetAxis();
	/// <summary>座ったパイロットの目の位置を返す</summary>
	/// <returns>機体の原点からの位置（エンジン座標。17 番 §5.1 の表）</returns>
	static Vector3 GetSeatedEye();

private:
	std::unique_ptr<Object3DInstance> cockpit_;
	std::unique_ptr<Object3DInstance> hatch_;

	Vector3 position_ = { 0.0f, 0.0f, 0.0f };
	float hatchOpenRatio_ = 1.0f;
	// Blender の −115° はエンジンでも同じ符号（どちらも +Z の前が上・後ろへ回る）
	float hatchOpenDegrees_ = -115.0f;
};

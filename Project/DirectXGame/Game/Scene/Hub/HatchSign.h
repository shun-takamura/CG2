#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <memory>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class SRVManager;
class RenderTexture;
class JsonValue;
struct ID3D12GraphicsCommandList;

/// <summary>
/// コクピットの中の宙に浮くハッチの表示板（18_HubTransitions.md ①「C で決まったこと」）。
/// ハッチが開いている間は赤とピンクの間の「HATCH UNLOCK ALERT」、閉じ切ったら緑の「HATCH LOCKED」。
/// 表示は TextRenderer で RenderTexture に描き（文字・縞・枠）、板ポリに貼ってライティングなしで描く。
/// 発光パスでは文字の所だけ光らせる（Material::emissive の w = 1）。
/// 位置は座ったパイロットの目からの相対（実機で見ながら Hub.json の "hatchSign" で詰める）。
/// </summary>
class HatchSign {
public:
	HatchSign();
	~HatchSign();
	void ResetToDefaults();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	void Finalize();

	/// <summary>LOCK / UNLOCK を切り替える（変わった瞬間は表示がチラつく）</summary>
	void SetLocked(bool locked);
	/// <summary>見え方 0..1（0 で描かない）</summary>
	void SetVisibility(float visibility) { visibility_ = visibility; }
	/// <summary>今の表示の色（LOCKED は緑・UNLOCK は赤とピンクの間）。コクピットの灯りと光る部位もこの色にする</summary>
	Vector3 GetColor() const {
		const Vector4& c = locked_ ? lockedColor_ : unlockColor_;
		return { c.x, c.y, c.z };
	}
	void Update(float deltaTime, const Vector3& mechOrigin);

	/// <summary>表示を RenderTexture に描く。シーンの RT を描き始める前に呼ぶ（RTV とビューポートを切り替える）</summary>
	void RenderSignTexture(ID3D12GraphicsCommandList* commandList);
	/// <summary>板を描く（Object3DManager::DrawSetting の後。半透明なので最後に）</summary>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パス</summary>
	void DrawBloom(DirectXCore* dxCore);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	void ApplyMaterial();

	std::unique_ptr<RenderTexture> texture_;
	std::unique_ptr<Object3DInstance> board_;
	bool locked_ = false;
	float visibility_ = 1.0f;
	float switchTime_ = 10.0f;   // 切り替わってからの秒（チラつき）
	float time_ = 0.0f;

	// 調整値（Hub.json の "hatchSign"）
	Vector3 offsetFromEye_ = { 0.0f, -0.08f, 0.55f };   // 座った目からの位置（エンジン座標、正面 +Z）
	Vector3 rotationDegrees_ = { 0.0f, 0.0f, 0.0f };
	float width_ = 0.56f;                                // 板の幅 [m]（高さは幅の 1/4）
	Vector4 unlockColor_ = { 1.0f, 0.28f, 0.55f, 1.0f }; // 赤とピンクの間
	Vector4 lockedColor_ = { 0.35f, 1.0f, 0.65f, 1.0f };
	float backgroundAlpha_ = 0.45f;                      // 文字の後ろの暗い帯の濃さ
	float emissive_ = 0.15f;                             // メイン描画で文字に足す明るさ
	float bloomScale_ = 8.0f;                            // 発光 RT に書く倍率
	float switchFlickerSeconds_ = 0.3f;
};

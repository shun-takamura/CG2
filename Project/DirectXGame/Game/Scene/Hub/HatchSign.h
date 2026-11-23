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
	/// <summary>調整値を既定にする</summary>
	HatchSign();
	~HatchSign();
	/// <summary>調整値（位置・大きさ・色・発光）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>表示を描く RenderTexture を作って外部テクスチャとして登録し、板のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>板の発光を戻し（モデルはタイトルのロゴと共有）、外部テクスチャの登録と RenderTexture を解放する</summary>
	void Finalize();

	/// <summary>LOCK / UNLOCK を切り替える（変わった瞬間は表示がチラつく）</summary>
	/// <param name="locked">true で HATCH LOCKED</param>
	void SetLocked(bool locked);
	/// <summary>見え方を設定する</summary>
	/// <param name="visibility">0〜1（0 で描かない）</param>
	void SetVisibility(float visibility) { visibility_ = visibility; }
	/// <summary>今の表示の色を返す。コクピットの灯りと光る部位もこの色にする</summary>
	/// <returns>LOCKED は緑・UNLOCK は赤とピンクの間（RGB 0〜1）</returns>
	Vector3 GetColor() const {
		const Vector4& c = locked_ ? lockedColor_ : unlockColor_;
		return { c.x, c.y, c.z };
	}
	/// <summary>時刻を進め、板を座ったパイロットの目からの位置に置いてマテリアルを整える</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="mechOrigin">機体の原点（足元の中央）のワールド位置</param>
	void Update(float deltaTime, const Vector3& mechOrigin);

	/// <summary>表示を RenderTexture に描く。シーンの RT を描き始める前に呼ぶ（RTV とビューポートを切り替える）</summary>
	/// <param name="commandList">描画のコマンドリスト</param>
	void RenderSignTexture(ID3D12GraphicsCommandList* commandList);
	/// <summary>板を描く（Object3DManager::DrawSetting の後。半透明なので最後に）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パスで板の文字の所を描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void DrawBloom(DirectXCore* dxCore);

	/// <summary>調整値（Hub.json の "hatchSign"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>板のマテリアルを表示のテクスチャ・ライティングなし・文字の所だけ発光にする（GPU の準備ができてから）</summary>
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

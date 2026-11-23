#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <memory>
#include <string>

class Camera;
class DirectXCore;
class SRVManager;
class Object3DManager;
class SpriteManager;
class SpriteInstance;
class WaterSurface;
class JsonValue;
class EtherVessel;

/// <summary>
/// 器の間（19_GameOverAndDifficulty.md §4.6・§4.7）：暗い水面に立つエーテルの器と、それを見るカメラ。
/// ゲームオーバー画面（GameOverScene）と HUB の賭ける画面（HubEtherBet）で同じ物を使う（2026-10-27 ユーザー：神殿の中には置かない）。
///   - 器（EtherVessel：G4 の 5 部品・G5 のガラスとエーテル・昇る糸と粒・支柱の光）
///   - 足元の暗い水面（WaterSurface。ボス戦の床と同じ石畳の底）。支柱を下りた光が泉に着くと波紋
///   - 背景の板（空を描かないので、シーン RT を不透明な色で塗る）
///   - カメラ（置き方は使う側が SetCameraPose で決める）
/// 調整値は GameOver.json の "background" / "water" / "vessel"（どちらの画面も同じ値を読む。保存はゲームオーバー画面の ImGui から）。
/// </summary>
class EtherVesselRoom {
public:
	EtherVesselRoom();
	~EtherVesselRoom();

	/// <summary>器・足元の水面・背景の板・カメラを用意する</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="spriteManager">背景の板に使う</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager, SpriteManager* spriteManager);
	/// <summary>器の終了処理をする</summary>
	void Finalize();

	/// <summary>この部屋を描き始める前に 1 回：既定のカメラ・映り込みの IBL・フォグを切り替える</summary>
	void Activate();

	/// <summary>カメラの置き方を決める</summary>
	/// <param name="offset">器の原点からの位置</param>
	/// <param name="rotateDegrees">回転 [度]（Rx·Ry の順。Y=180 で -Z を向く）</param>
	void SetCameraPose(const Vector3& offset, const Vector3& rotateDegrees);
	/// <summary>部屋のカメラを返す</summary>
	/// <returns>カメラ。Initialize 前は nullptr</returns>
	Camera* GetCamera() const { return camera_.get(); }
	/// <summary>器を返す</summary>
	/// <returns>器。Initialize 前は nullptr</returns>
	EtherVessel* GetVessel() const { return vessel_.get(); }
	/// <summary>映り込みに使う IBL のパスを返す</summary>
	/// <returns>パス</returns>
	const std::string& GetIblPath() const { return iblPath_; }
	/// <summary>器の原点（土台の脚の下。水面の真ん中）を返す</summary>
	/// <returns>ワールド位置</returns>
	const Vector3& GetOrigin() const { return origin_; }

	/// <summary>器の足元の水面に波紋を立てる（選択肢のカーソル・値の調整などで）</summary>
	void EmitCursorRing();

	/// <summary>
	/// 針の先の純度の札を TextRenderer に積む（Flush は呼ぶ側）。暗い箱に金の枠で「5.0」、上に小さく「純度」。
	/// 札は針と一緒に文字盤の弧を動く。賭ける画面とゲームオーバー画面で共用
	/// </summary>
	/// <param name="purity">表示する純度</param>
	/// <param name="alpha">不透明度 0〜1（0 以下なら描かない）</param>
	/// <param name="scale">札の大きさの倍率</param>
	/// <param name="offset">針の先から、球の中心 → 針の先の向きへずらす量 [px]</param>
	/// <param name="gold">枠と文字の金の色（RGB 0〜1）</param>
	void DrawPurityTag(float purity, float alpha, float scale, float offset, const float (&gold)[3]) const;
	/// <summary>ワールドの点を UI 座標へ投影する</summary>
	/// <param name="world">ワールドの点</param>
	/// <param name="out">UI 座標の受け取り先</param>
	/// <returns>カメラの後ろなら false</returns>
	bool ProjectToUi(const Vector3& world, Vector2& out) const;

	/// <summary>カメラ・器・水面・背景を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);
	/// <summary>波のシミュレーション → 背景 → 器 → 水面 → ガラス・支柱の光・糸の順に描く。粒（DrawGlobalEffects）と文字は呼ぶ側が後に</summary>
	void Draw();
	/// <summary>発光パスで器を描く</summary>
	void DrawBloom();

	/// <summary>調整値（GameOver.json の "background" / "water" / "vessel"）を読む</summary>
	/// <param name="root">GameOver.json の根</param>
	void LoadTuning(const JsonValue& root);
	/// <summary>調整値を GameOver.json の根へ書く</summary>
	/// <param name="root">書き込み先の根</param>
	void SaveTuning(JsonValue& root) const;
	/// <summary>水面・背景の調整値の ImGui を表示する</summary>
	void OnImGuiWater();

private:
	/// <summary>水面の調整値（深さ・空の映り込み・波紋・色・高さ）を水面へ反映する</summary>
	void ApplyWaterTuning();

	DirectXCore* dxCore_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SpriteManager* spriteManager_ = nullptr;
	std::unique_ptr<Camera> camera_;
	std::unique_ptr<EtherVessel> vessel_;
	std::unique_ptr<WaterSurface> water_;
	std::unique_ptr<SpriteInstance> backgroundSprite_;
	std::string iblPath_;
	// 器の原点（土台の脚の下）。水面の真ん中に立つ
	Vector3 origin_{ 0.0f, 0.0f, 0.0f };

	// ----- 調整値（GameOver.json） -----
	float background_[3] = { 0.01f, 0.012f, 0.02f };
	float waterHeight_ = 0.03f;
	float waterDepth_ = 0.6f;
	Vector3 waterColor_{ 0.01f, 0.03f, 0.05f };
	float waterSkyIntensity_ = 0.35f;
	float waterRingAmplitude_ = 0.004f;   // 待機中のさざ波
	float waterRingInterval_ = 5.0f;
	float cursorRingAmplitude_ = 0.012f;  // カーソルを動かしたときの波紋
	float cursorRingWavelength_ = 0.25f;
	float cursorRingRadius_ = 6.0f;
	float arrivalRingAmplitude_ = 0.008f; // 支柱を下りた光が泉に着いたときの波紋
	float topArrivalSplash_ = 0.0015f;    // 支柱を昇った光が球に着いたときの液面の揺れ（賭ける）
};

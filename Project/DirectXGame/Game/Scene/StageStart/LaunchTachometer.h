#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <string_view>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class SRVManager;
class RenderTexture;
class JsonValue;
struct ID3D12GraphicsCommandList;

/// <summary>
/// スラスターの出力のタコメーター（16_StageStart.md §3.2、F5）。2026-10-19 ユーザー決定：SPACE はスラスターの出力で、
/// 長押しの間メーターが上がり、離すと下がる。100% で射出。カタパルトの充電が済むまでは受け付けない（STANDBY）。
/// 見た目は表示板 LAUNCH CONTROL と同じ緑のネオン。TextRenderer は傾いた四角を描けないので、
/// 小さい四角を弧に並べた LED のセグメント（緑 → 黄 → 赤）と点を並べた針で描く。
/// 置き場所（2026-10-19 ユーザーのスケッチ）：一人称は確認パネルの右に浮く板、三人称は画面の右下に見えるパネル
/// （確認パネルと同じく、三人称のカメラの画面の位置と距離から置く。LaunchControlBoard::PlaceOnScreen）。
/// 中身は RenderTexture（kSize × kSize）に描き、板のモデル（logo_plane.mesh）は確認パネルと共有なので描く直前に絵を貼る。
/// 調整値は StagePlay.json の "stageStart" → "tachometer"。
/// </summary>
class LaunchTachometer {
public:
	enum class Mode {
		Standby,   // カタパルトの充電中（SPACE を受け付けない）
		Ready,     // 充電済み：HOLD [SPACE]
		Launch,    // 射出中
	};

	LaunchTachometer();
	~LaunchTachometer();
	void ResetToDefaults();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	void Finalize();

	/// <summary>
	/// thrust＝出力 0..1、rpm / maxRpm＝回転数、redZone＝赤の始まり（出力）、modeTime＝今の Mode に入ってからの秒、
	/// appearTime＝出始めてからの秒（負で出さない）
	/// </summary>
	void SetState(float thrust, float rpm, float maxRpm, float redZone, Mode mode, float modeTime, float appearTime);
	/// <summary>LaunchControlBoard::Update と同じ引数（一人称は座った目から、三人称はカメラの画面の位置から置く）</summary>
	void Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye, const Vector3& thirdPersonTarget,
		float fovY, float aspect);

	/// <summary>中身を RenderTexture に描く。シーンの RT を描き始める前に呼ぶ</summary>
	void RenderMeterTexture(ID3D12GraphicsCommandList* commandList);
	void Draw(DirectXCore* dxCore, bool firstPerson);
	void DrawBloom(DirectXCore* dxCore, bool firstPerson);
	bool IsShown() const { return appearTime_ >= 0.0f; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

	static constexpr float kSize = 600.0f;

private:
	void DrawContents();
	void Text(std::string_view text, float x, float y, float size, const Vector4& color);
	void TextCentered(std::string_view text, float cx, float y, float size, const Vector4& color);
	void Rect(float x, float y, float w, float h, const Vector4& color);
	void Frame(float x, float y, float w, float h, float thickness, const Vector4& color);
	void ApplyMaterial();
	void BindTexture(Object3DInstance& target);

	float alpha_ = 1.0f;
	std::unique_ptr<RenderTexture> texture_;
	std::unique_ptr<Object3DInstance> board_;   // 一人称：宙に浮く板
	std::unique_ptr<Object3DInstance> panel_;   // 三人称：画面の右下に見えるパネル
	float thrust_ = 0.0f;
	float rpm_ = 0.0f;
	float maxRpm_ = 12000.0f;
	float redZone_ = 0.85f;
	Mode mode_ = Mode::Standby;
	float modeTime_ = 0.0f;
	float appearTime_ = -1.0f;
	float time_ = 0.0f;

	// 調整値
	Vector3 offsetFromEye_ = { 0.27f, -0.17f, 0.62f };      // 一人称：座った目からの位置（確認パネルの右）
	Vector3 rotationDegrees_ = { 10.0f, 18.0f, 0.0f };
	float width_ = 0.36f;                                    // 一人称：板の幅 [m]
	// 三人称：画面の右下。カメラは床から 1.5m なので、遠くに置くと床に埋まって見えなかった（2026-10-19 実機）→ カメラの近くに
	Vector2 panelScreenAnchor_ = { 0.62f, -0.5f };
	float panelDistance_ = 3.0f;
	bool panelFaceEye_ = true;
	Vector3 panelAngleOffsetDegrees_ = { 0.0f, 10.0f, 0.0f };
	float panelScreenWidth_ = 0.2f;
	float emissive_ = 0.06f;
	float bloomScale_ = 1.2f;
	float appearSeconds_ = 0.35f;
	float backgroundAlpha_ = 0.85f;
	int segmentCount_ = 36;                                  // 弧の LED の数
	float sweepDegrees_ = 270.0f;                            // 弧の開き
	Vector4 green_ = { 0.35f, 1.0f, 0.6f, 1.0f };
	Vector4 dimGreen_ = { 0.12f, 0.45f, 0.25f, 1.0f };
	Vector4 yellow_ = { 1.0f, 0.82f, 0.2f, 1.0f };
	Vector4 red_ = { 1.0f, 0.25f, 0.38f, 1.0f };
	float yellowFrom_ = 0.65f;                               // 黄に変わる出力（赤は CatapultLaunch の redZone）
};

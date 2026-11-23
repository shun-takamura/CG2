#include "LaunchControlBoard.h"

#include "LaunchChecklist.h"
#include "Cockpit/CockpitModel.h"
#include "Hub/CockpitReveal.h"
#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "Material.h"
#include "RenderTexture.h"
#include "TextureManager.h"
#include "TextRenderer.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 点滅の明るさの真ん中
	constexpr float kBlinkMid = 0.75f;
	// 点滅の明るさの振れ幅
	constexpr float kBlinkAmplitude = 0.25f;
	// 座った目から表示板までのずれの初期値 [m]
	constexpr Vector3 kDefaultOffsetFromEye = { -0.24f, -0.15f, 0.62f };
	// 表示板の傾きの初期値 [度]
	constexpr Vector3 kDefaultRotationDegrees = { 10.0f, -14.0f, 0.0f };
	// 表示板の幅の初期値 [m]
	constexpr float kDefaultWidth = 0.62f;
	// 三人称で表示板を置く画面の位置の初期値
	constexpr Vector2 kDefaultPanelScreenAnchor = { -0.36f, 0.40f };
	// 三人称で表示板を置くカメラからの距離の初期値 [m]
	constexpr float kDefaultPanelDistance = 20.0f;
	// 三人称で表示板に足す傾きの初期値 [度]
	constexpr Vector3 kDefaultPanelAngleOffsetDegrees = { 0.0f, -10.0f, 0.0f };
	// 三人称での表示板の幅（画面に対する割合）の初期値
	constexpr float kDefaultPanelScreenWidth = 0.385f;
	// 表示板の発光の初期値
	constexpr float kDefaultEmissive = 0.06f;
	// 表示板のブルームの倍率の初期値
	constexpr float kDefaultBloomScale = 1.2f;
	// 表示板がチカチカして出る長さの初期値 [秒]
	constexpr float kDefaultAppearSeconds = 0.35f;
	// 地の不透明度の初期値
	constexpr float kDefaultBackgroundAlpha = 0.85f;
	// 緑の初期値
	constexpr Vector4 kDefaultGreen = { 0.35f, 1.0f, 0.6f, 1.0f };
	// 暗い緑の初期値
	constexpr Vector4 kDefaultDimGreen = { 0.12f, 0.45f, 0.25f, 1.0f };
	// 黄色の初期値
	constexpr Vector4 kDefaultYellow = { 1.0f, 0.82f, 0.2f, 1.0f };
	// 赤の初期値
	constexpr Vector4 kDefaultRed = { 1.0f, 0.25f, 0.38f, 1.0f };
	// CLEAR がチカチカして出る長さ [秒]
	constexpr float kClearFlickerSeconds = 0.25f;
	// CHECK の点滅の速さ [回/秒]
	constexpr float kCheckBlinkHz = 1.2f;
	// 状態の箱・ランプの太い枠の太さ [px]
	constexpr float kThickFrameThickness = 2.0f;
	// 地の赤
	constexpr float kBackgroundRed = 0.01f;
	// 地の緑
	constexpr float kBackgroundGreen = 0.05f;
	// 地の青
	constexpr float kBackgroundBlue = 0.03f;
	// 外の枠・キーの枠・点線の細い線の太さ [px]
	constexpr float kThinFrameThickness = 1.5f;
	// LAUNCH CONTROL の上端 [px]
	constexpr float kTitleY = 22.0f;
	// 右上の札の文字の大きさ
	constexpr float kTagScale = 0.5f;
	// 右上の札の上端 [px]
	constexpr float kTagY = 44.0f;
	// 見出しの下の黄色い帯の上端 [px]
	constexpr float kHeaderBarY = 84.0f;
	// 見出しの下の黄色い帯の幅 [px]
	constexpr float kHeaderBarWidth = 150.0f;
	// 見出しの下の黄色い帯の高さ [px]
	constexpr float kHeaderBarHeight = 6.0f;
	// 見出しの下の緑の線の左端（余白から）[px]
	constexpr float kHeaderLineOffsetX = 160.0f;
	// 見出しの下の緑の線の上端 [px]
	constexpr float kHeaderLineY = 86.0f;
	// 見出しの下の緑の線の太さ [px]
	constexpr float kHeaderLineThickness = 2.0f;
	// 行の番号を行の上からどれだけ下に置くか [px]
	constexpr float kRowIndexOffsetY = 14.0f;
	// 行の番号の文字の大きさ
	constexpr float kRowIndexScale = 0.6f;
	// 項目名・説明を余白からどれだけ右に置くか [px]
	constexpr float kRowLabelOffsetX = 50.0f;
	// 項目名を行の上からどれだけ下に置くか [px]
	constexpr float kRowLabelOffsetY = 6.0f;
	// 項目名と説明の間 [px]
	constexpr float kRowHelpGap = 8.0f;
	// 説明の文字の大きさ
	constexpr float kRowHelpScale = 0.55f;
	// 説明の色を暗い緑から緑へ寄せる割合
	constexpr float kRowHelpMix = 0.4f;
	// 行の区切りの点線の間隔 [px]
	constexpr float kDashPitch = 14.0f;
	// 行の区切りを行の下端からどれだけ上に置くか [px]
	constexpr float kRowSeparatorInset = 4.0f;
	// 行の区切りの点線の 1 つの長さ [px]
	constexpr float kDashLength = 7.0f;
	// キーの案内の文字の大きさの既定
	constexpr float kKeyHintScale = 0.55f;
	// キーの案内の枠の幅に足す左右の余白の合計 [px]
	constexpr float kKeyHintPaddingX = 20.0f;
	// キーの案内の枠の高さに足す上下の余白の合計 [px]
	constexpr float kKeyHintPaddingY = 12.0f;
	// キーの案内の文字を枠の左からどれだけ右に置くか [px]
	constexpr float kKeyHintTextOffsetX = 10.0f;
	// キーの案内の文字を枠の上からどれだけ下に置くか [px]
	constexpr float kKeyHintTextOffsetY = 6.0f;
	// 給電ワイヤーの流れるランプの数
	constexpr int kCableLampCount = 6;
	// 給電ワイヤーのランプが流れる速さ [周期/秒]
	constexpr float kCableFlowSpeed = 2.0f;
	// 隣のランプとの流れのずれ
	constexpr float kCableLampPhaseStep = 0.25f;
	// 給電ワイヤーのランプの流れの周期
	constexpr float kCableFlowPeriod = 1.5f;
	// 給電ワイヤーのランプの間隔 [px]
	constexpr float kCableLampPitch = 34.0f;
	// 給電ワイヤーのランプを行の上からどれだけ下に置くか [px]
	constexpr float kCableLampOffsetY = 40.0f;
	// 給電ワイヤーのランプの幅 [px]
	constexpr float kCableLampWidth = 24.0f;
	// 給電ワイヤーのランプの高さ [px]
	constexpr float kCableLampHeight = 16.0f;
	// 流れの周期のうちランプが点いている所
	constexpr float kCableLampOnPhase = 0.5f;
	// 消えているランプの不透明度
	constexpr float kCableLampOffAlpha = 0.35f;
	// MOUSE の案内を行の上からどれだけ下に置くか [px]
	constexpr float kAimHintOffsetY = 8.0f;
	// 照準のゲージの幅 [px]
	constexpr float kAimBarWidth = 240.0f;
	// 照準のゲージの枠を行の上からどれだけ下に置くか [px]
	constexpr float kAimBarOffsetY = 62.0f;
	// 照準のゲージの枠の高さ [px]
	constexpr float kAimBarHeight = 16.0f;
	// 照準のゲージの中身の左の余白 [px]
	constexpr float kAimFillInset = 3.0f;
	// 照準のゲージの中身を行の上からどれだけ下に置くか [px]
	constexpr float kAimFillOffsetY = 65.0f;
	// 照準のゲージの中身を枠の幅から縮める量（左右の余白の合計）[px]
	constexpr float kAimFillWidthTrim = 6.0f;
	// 照準のゲージの中身の高さ [px]
	constexpr float kAimFillHeight = 10.0f;
	// スラスターのランプの間隔 [px]
	constexpr float kThrusterLampPitch = 60.0f;
	// スラスター・射撃のランプを行の上からどれだけ下に置くか [px]
	constexpr float kLampOffsetY = 26.0f;
	// スラスターのランプの一辺 [px]
	constexpr float kThrusterLampSize = 48.0f;
	// スラスターのランプの文字の中心（ランプの左から）[px]
	constexpr float kThrusterLampTextX = 24.0f;
	// スラスターのランプの文字の上端（ランプの上から）[px]
	constexpr float kThrusterLampTextY = 8.0f;
	// 済んだスラスターのランプの塗りの不透明度
	constexpr float kThrusterDoneAlpha = 0.25f;
	// L-CLICK と射撃のランプの間 [px]
	constexpr float kShotLampGap = 16.0f;
	// 射撃のランプどうしのすき間 [px]
	constexpr float kShotLampSpacing = 8.0f;
	// 下の段の文字を下の段の上からどれだけ下に置くか [px]
	constexpr float kFooterTextOffsetY = 18.0f;
	// SYSTEM CHECK と LAUNCH UNABLE の間 [px]
	constexpr float kFooterSubLineGap = 6.0f;
	// 下の段の 2 行目の文字の大きさ
	constexpr float kFooterSubScale = 0.6f;
	// 下の段がチカチカして出る長さ [秒]
	constexpr float kFooterFlickerSeconds = 0.3f;
	// COURSE CLEAR を下の段の上からどれだけ下に置くか [px]
	constexpr float kCourseClearOffsetY = 10.0f;
	// COURSE CLEAR の文字の大きさ（下の段の文字に対する倍率）
	constexpr float kCourseClearScale = 1.15f;
	// 流れる点が 1 秒に増える数
	constexpr float kDotsPerSecond = 4.0f;
	// 流れる点の数の周期（0〜3 個）
	constexpr float kDotCycle = 4.0f;
	// POWER-UP の行を中央からどれだけ左に置くか [px]
	constexpr float kPowerUpOffsetX = 170.0f;
	// COURSE CLEAR と POWER-UP の間 [px]
	constexpr float kPowerUpGap = 8.0f;
	// 下の段の箱の上の余白 [px]
	constexpr float kFooterBoxInsetY = 8.0f;
	// 下の段の箱を下の段の高さから縮める量（上下の余白の合計）[px]
	constexpr float kFooterBoxHeightTrim = 16.0f;
	// LAUNCH READY の箱の不透明度
	constexpr float kReadyBoxAlpha = 0.9f;
	// LAUNCH READY の箱と充電の箱の間 [px]
	constexpr float kChargeBoxGap = 20.0f;
	// 充電の箱の文字の大きさ（下の段の文字に対する倍率）
	constexpr float kChargeTextScale = 0.9f;
	// CHARGING の点滅の速さ [回/秒]
	constexpr float kChargeBlinkHz = 1.6f;

	// 板ポリ（幅 2 × 高さ 1、表が -Z）。タイトルのロゴ・HUB の HatchSign と共有
	constexpr const char* kBoardModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kBoardMesh = "logo_plane.mesh";
	// RenderTexture をモデルのテクスチャとして引く名前
	constexpr const char* kTextureName = "LaunchControlBoard_RenderTexture";

	// 板の座標の割り付け
	constexpr float kPad = 40.0f;          // 左右の余白
	constexpr float kRowTop = 118.0f;      // 1 行目の上
	constexpr float kRowHeight = 112.0f;
	constexpr float kWidgetX = 440.0f;     // 行の真ん中（キーの表示・ゲージ・ランプ）
	constexpr float kStatusW = 200.0f;     // 行の右の CLEAR の箱
	constexpr float kStatusH = 60.0f;
	// 主な文字の大きさ（2026-10-19 ユーザー指示で少し大きく）
	constexpr float kTitleSize = 1.65f;    // LAUNCH CONTROL
	constexpr float kLabelSize = 1.15f;    // 項目名
	constexpr float kStatusSize = 1.15f;   // 右の箱（ON-LINE / CHECK / CLEAR）
	constexpr float kFooterSize = 1.4f;    // 下の段（SYSTEM CHECK など）
	constexpr float kGlyphHeight = 32.0f;  // 大きさ 1 の文字の高さ [px]（TextRenderer の pixelHeight）
	constexpr float kFooterTop = kRowTop + kRowHeight * 4.0f + 10.0f;

	const Vector4 kInk = { 0.02f, 0.07f, 0.04f, 1.0f };   // 白地・黄地の上の文字

	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">元の色</param>
	/// <param name="alpha">α に掛ける倍率</param>
	/// <returns>α だけ変えた色</returns>
	Vector4 WithAlpha(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}
	/// <summary>2 つの色を t で線形補間する</summary>
	/// <param name="a">t = 0 の色</param>
	/// <param name="b">t = 1 の色</param>
	/// <param name="t">補間の割合</param>
	/// <returns>補間した色</returns>
	Vector4 Mix(const Vector4& a, const Vector4& b, float t) {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
	}
	/// <summary>点滅の明るさを求める（0.5〜1 を cos で行き来する）</summary>
	/// <param name="time">経過 [秒]</param>
	/// <param name="hz">点滅の速さ [回/秒]</param>
	/// <returns>明るさ 0.5〜1</returns>
	float Blink(float time, float hz) {
		return kBlinkMid + kBlinkAmplitude * std::cos(time * hz * 2.0f * kPi);
	}
}

LaunchControlBoard::LaunchControlBoard() {
	ResetToDefaults();
}

LaunchControlBoard::~LaunchControlBoard() = default;

void LaunchControlBoard::ResetToDefaults() {
	offsetFromEye_ = kDefaultOffsetFromEye;
	rotationDegrees_ = kDefaultRotationDegrees;
	width_ = kDefaultWidth;
	panelScreenAnchor_ = kDefaultPanelScreenAnchor;
	panelDistance_ = kDefaultPanelDistance;
	panelFaceEye_ = true;
	panelAngleOffsetDegrees_ = kDefaultPanelAngleOffsetDegrees;
	panelScreenWidth_ = kDefaultPanelScreenWidth;
	emissive_ = kDefaultEmissive;
	bloomScale_ = kDefaultBloomScale;
	appearSeconds_ = kDefaultAppearSeconds;
	backgroundAlpha_ = kDefaultBackgroundAlpha;
	green_ = kDefaultGreen;
	dimGreen_ = kDefaultDimGreen;
	yellow_ = kDefaultYellow;
	red_ = kDefaultRed;
}

void LaunchControlBoard::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager) {
	texture_ = std::make_unique<RenderTexture>();
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	texture_->Initialize(dxCore, srvManager, static_cast<uint32_t>(kWidth), static_cast<uint32_t>(kHeight),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
	TextureManager::GetInstance()->RegisterExternalTexture(kTextureName, texture_->GetResource(), texture_->GetSRVIndex());

	board_ = std::make_unique<Object3DInstance>();
	board_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchControlBoard");
	panel_ = std::make_unique<Object3DInstance>();
	panel_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchControlPanel");
	checklist_ = nullptr;
	appearTime_ = -1.0f;
}

void LaunchControlBoard::Finalize() {
	// 板のモデルはタイトルのロゴ・HatchSign と共有なので発光を戻す（テクスチャは使う側が毎フレーム貼り直す）
	if (board_) {
		if (ModelInstance* model = board_->GetModelInstance()) model->ClearEmissive();
	}
	TextureManager::GetInstance()->UnregisterExternalTexture(kTextureName);
	if (texture_) {
		texture_->Finalize();
		texture_.reset();
	}
	board_.reset();
	panel_.reset();
}

void LaunchControlBoard::SetState(const LaunchChecklist* checklist, Status status, float statusTime, float appearTime,
	bool cablePurged) {
	checklist_ = checklist;
	status_ = status;
	statusTime_ = statusTime;
	appearTime_ = appearTime;
	cablePurged_ = cablePurged;
}

void LaunchControlBoard::Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye,
	const Vector3& thirdPersonTarget, float fovY, float aspect) {
	time_ += deltaTime;
	if (!board_ || !panel_) return;
	// 板ポリは 2 × 1。表示の縦横比に合わせる
	constexpr float kAspect = kHeight / kWidth;

	const Vector3 eye = CockpitModel::GetSeatedEye();
	board_->SetTranslate({ mechOrigin.x + eye.x + offsetFromEye_.x, mechOrigin.y + eye.y + offsetFromEye_.y,
		mechOrigin.z + eye.z + offsetFromEye_.z });
	board_->SetRotate({ DegToRad(rotationDegrees_.x), DegToRad(rotationDegrees_.y), DegToRad(rotationDegrees_.z) });
	board_->SetScale({ width_ * 0.5f, width_ * kAspect * 0.5f, 1.0f });
	board_->Update();

	// 据え付けたパネル：三人称のカメラの画面の panelScreenAnchor に見える所
	PlaceOnScreen(*panel_, panelScreenAnchor_, panelDistance_, panelFaceEye_, panelAngleOffsetDegrees_, panelScreenWidth_, kAspect,
		thirdPersonEye, thirdPersonTarget, fovY, aspect);
	panel_->Update();
	ApplyMaterial();
}

void LaunchControlBoard::PlaceOnScreen(Object3DInstance& panel, const Vector2& anchor, float distance, bool faceEye,
	const Vector3& angleOffsetDegrees, float screenWidth, float heightPerWidth,
	const Vector3& eye, const Vector3& target, float fovY, float aspect) {
	// カメラの基底（左手系。right = up × forward、up = forward × right）から、画面の anchor に見える向きへ distance 進んだ所
	const Vector3 f = Normalize(Vector3{ target.x - eye.x, target.y - eye.y, target.z - eye.z });
	const Vector3 r = Normalize(Vector3{ f.z, 0.0f, -f.x });
	const Vector3 u = { f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x };
	const float tanV = std::tan(fovY * 0.5f);
	const float sx = anchor.x * tanV * aspect;
	const float sy = anchor.y * tanV;
	const Vector3 dir = Normalize(Vector3{ f.x + r.x * sx + u.x * sy, f.y + r.y * sx + u.y * sy, f.z + r.z * sx + u.z * sy });
	panel.SetTranslate({ eye.x + dir.x * distance, eye.y + dir.y * distance, eye.z + dir.z * distance });
	// 板ポリの表 -Z をカメラの側へ。yaw 0 が +Z・pitch は正が下向き（StageStartPart::ApplyCamera と同じ）。
	// 目へ向けるなら板からカメラへの向き（-dir）、画面と平行ならカメラの前（f）を使う。そこから少しずらす
	const Vector3& face = faceEye ? dir : f;
	const float yaw = std::atan2(face.x, face.z);
	const float pitch = -std::atan2(face.y, std::sqrt(face.x * face.x + face.z * face.z));
	panel.SetRotate({ pitch + DegToRad(angleOffsetDegrees.x), yaw + DegToRad(angleOffsetDegrees.y), DegToRad(angleOffsetDegrees.z) });
	// 画面の幅（その距離で 2·tanH·距離）に対する割合からワールドの幅を出す
	const float width = screenWidth * 2.0f * tanV * aspect * distance;
	panel.SetScale({ width * 0.5f, width * heightPerWidth * 0.5f, 1.0f });
}

void LaunchControlBoard::ApplyMaterial() {
	ModelInstance* model = board_ ? board_->GetModelInstance() : nullptr;
	// テクスチャの差し替えは GPU の準備後（準備時に .mat のパスで上書きされるため）
	if (!model || !model->IsGPUReady()) return;
	if (model->GetTextureFilePath() != kTextureName) model->SetTextureFilePath(kTextureName);
	for (const RenderSubmesh& sm : model->GetSubmeshes()) {
		if (!sm.material) continue;
		// 光の当たり方で明暗が出ないよう、描いた色をそのまま出す
		sm.material->enableLighting = 0;
		sm.material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		sm.material->dissolveEnable = kDissolveOff;
	}
	// 文字の所だけ光らせる（テクスチャを掛けた発光）
	model->SetEmissiveByMaterialName("", { emissive_, emissive_, emissive_ }, 1.0f);
}

void LaunchControlBoard::RenderBoardTexture(ID3D12GraphicsCommandList* commandList) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!texture_ || !tr->IsInitialized() || !IsShown()) return;
	texture_->BeginRender(commandList);   // 透明でクリア
	tr->BeginOffscreen(kWidth, kHeight);
	DrawContents();
	tr->FlushOffscreen();
	texture_->EndRender(commandList);
}

void LaunchControlBoard::Draw(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	// 板のモデルはタコメーターと共有なので、描く直前にこの板の絵を貼る（テクスチャは描くときに引かれる）
	if (ModelInstance* model = target->GetModelInstance(); model && model->IsGPUReady()) model->SetTextureFilePath(kTextureName);
	target->Draw(dxCore);
}

void LaunchControlBoard::DrawBloom(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	if (ModelInstance* model = target->GetModelInstance(); model && model->IsGPUReady()) model->SetTextureFilePath(kTextureName);
	target->DrawBloomPass(dxCore, bloomScale_);
}

// ---------------------------------------------------------------- 積む（RenderTexture のピクセル）

void LaunchControlBoard::Text(std::string_view text, float x, float y, float size, const Vector4& color) {
	TextRenderer::GetInstance()->DrawText(text, { x, y }, size, WithAlpha(color, alpha_));
}

void LaunchControlBoard::TextCentered(std::string_view text, float cx, float y, float size, const Vector4& color) {
	const float width = TextRenderer::GetInstance()->MeasureWidth(text, size);
	Text(text, cx - width * 0.5f, y, size, color);
}

void LaunchControlBoard::Rect(float x, float y, float w, float h, const Vector4& color) {
	TextRenderer::GetInstance()->DrawRect({ x, y }, { w, h }, WithAlpha(color, alpha_));
}

void LaunchControlBoard::Frame(float x, float y, float w, float h, float thickness, const Vector4& color) {
	Rect(x, y, w, thickness, color);
	Rect(x, y + h - thickness, w, thickness, color);
	Rect(x, y, thickness, h, color);
	Rect(x + w - thickness, y, thickness, h, color);
}

void LaunchControlBoard::DrawStatusBox(float x, float y, float w, float h, bool cleared, float sinceCleared) {
	// CLEAR になった瞬間は蛍光灯のようにチラついて白地に変わる
	const float on = cleared ? CockpitReveal::Flicker(sinceCleared, kClearFlickerSeconds) : 0.0f;
	if (on > 0.0f) {
		Rect(x, y, w, h, WithAlpha({ 1.0f, 1.0f, 1.0f, 1.0f }, on));
		TextCentered("CLEAR", x + w * 0.5f, y + (h - kGlyphHeight * kStatusSize) * 0.5f, kStatusSize, WithAlpha(kInk, on));
	}
	if (on < 1.0f) {
		const float blink = Blink(time_, kCheckBlinkHz) * (1.0f - on);
		Frame(x, y, w, h, kThickFrameThickness, WithAlpha(red_, blink));
		TextCentered("CHECK", x + w * 0.5f, y + (h - kGlyphHeight * kStatusSize) * 0.5f, kStatusSize, WithAlpha(red_, blink));
	}
}

void LaunchControlBoard::DrawContents() {
	if (!checklist_) return;
	alpha_ = CockpitReveal::Flicker(appearTime_, appearSeconds_);
	if (alpha_ <= 0.0f) return;
	const LaunchChecklist& c = *checklist_;
	const float right = kWidth - kPad;

	// 地・枠・四隅の鉤
	Rect(0.0f, 0.0f, kWidth, kHeight, { kBackgroundRed, kBackgroundGreen, kBackgroundBlue, backgroundAlpha_ });
	Frame(0.0f, 0.0f, kWidth, kHeight, kThinFrameThickness, dimGreen_);
	constexpr float kCorner = 26.0f;
	constexpr float kCornerThick = 4.0f;
	for (const float cx : { 0.0f, kWidth - kCorner }) {
		for (const float cy : { 0.0f, kHeight - kCornerThick }) Rect(cx, cy, kCorner, kCornerThick, green_);
	}
	for (const float cx : { 0.0f, kWidth - kCornerThick }) {
		for (const float cy : { 0.0f, kHeight - kCorner }) Rect(cx, cy, kCornerThick, kCorner, green_);
	}

	// 見出し
	Text("LAUNCH CONTROL", kPad, kTitleY, kTitleSize, green_);
	const char* tag = "CATAPULT-01 // SORTIE";
	const float tagW = TextRenderer::GetInstance()->MeasureWidth(tag, kTagScale);
	Text(tag, right - tagW, kTagY, kTagScale, dimGreen_);
	Rect(kPad, kHeaderBarY, kHeaderBarWidth, kHeaderBarHeight, yellow_);
	Rect(kPad + kHeaderLineOffsetX, kHeaderLineY, right - kPad - kHeaderLineOffsetX, kHeaderLineThickness, green_);

	// 行
	auto rowY = [](int row) { return kRowTop + kRowHeight * static_cast<float>(row); };
	auto rowFrame = [&](int row, const char* index, const char* label, const char* help) {
		const float y = rowY(row);
		Text(index, kPad, y + kRowIndexOffsetY, kRowIndexScale, dimGreen_);
		Text(label, kPad + kRowLabelOffsetX, y + kRowLabelOffsetY, kLabelSize, green_);
		Text(help, kPad + kRowLabelOffsetX, y + kRowLabelOffsetY + kGlyphHeight * kLabelSize + kRowHelpGap, kRowHelpScale, Mix(dimGreen_, green_, kRowHelpMix));
		// 行の区切り（点線）
		for (float x = kPad; x < right; x += kDashPitch) Rect(x, y + kRowHeight - kRowSeparatorInset, kDashLength, kThinFrameThickness, dimGreen_);
	};
	auto keyHint = [&](const char* key, float x, float y, float size = kKeyHintScale) {
		const float w = TextRenderer::GetInstance()->MeasureWidth(key, size) + kKeyHintPaddingX;
		const float h = kGlyphHeight * size + kKeyHintPaddingY;
		Frame(x, y, w, h, kThinFrameThickness, yellow_);
		Text(key, x + kKeyHintTextOffsetX, y + kKeyHintTextOffsetY, size, yellow_);
		return w;
	};
	const float statusX = right - kStatusW;
	auto statusY = [&](int row) { return rowY(row) + (kRowHeight - kRowSeparatorInset - kStatusH) * 0.5f; };

	// 0. 給電ワイヤー（操作しない。黄の ON-LINE、切り離しで赤の PURGE）
	{
		const float y = rowY(0);
		rowFrame(0, "00", "POWER CABLE", "給電ワイヤー 接続中");
		for (int i = 0; i < kCableLampCount; ++i) {
			const float phase = std::fmod(time_ * kCableFlowSpeed + static_cast<float>(i) * kCableLampPhaseStep, kCableFlowPeriod);
			Rect(kWidgetX + static_cast<float>(i) * kCableLampPitch, y + kCableLampOffsetY, kCableLampWidth, kCableLampHeight,
				WithAlpha(cablePurged_ ? red_ : yellow_, phase < kCableLampOnPhase && !cablePurged_ ? 1.0f : kCableLampOffAlpha));
		}
		const Vector4& col = cablePurged_ ? red_ : yellow_;
		const float sy = statusY(0);
		Frame(statusX, sy, kStatusW, kStatusH, kThickFrameThickness, col);
		TextCentered(cablePurged_ ? "PURGE" : "ON-LINE", statusX + kStatusW * 0.5f, sy + (kStatusH - kGlyphHeight * kStatusSize) * 0.5f,
			kStatusSize, col);
	}

	// 1. 照準
	{
		const float y = rowY(1);
		rowFrame(1, "01", "AIM CONTROL", "マウス / 右スティックで照準を動かす");
		keyHint("MOUSE", kWidgetX, y + kAimHintOffsetY);
		const float barW = kAimBarWidth;
		Frame(kWidgetX, y + kAimBarOffsetY, barW, kAimBarHeight, kThinFrameThickness, dimGreen_);
		Rect(kWidgetX + kAimFillInset, y + kAimFillOffsetY, (barW - kAimFillWidthTrim) * c.GetAimProgress(), kAimFillHeight, green_);
		DrawStatusBox(statusX, statusY(1), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Aim),
			c.TimeSinceCleared(LaunchChecklist::Item::Aim));
	}

	// 2. スラスター（W A S D の 4 つのランプ）
	{
		const float y = rowY(2);
		rowFrame(2, "02", "THRUSTER", "W A S D / 左スティックで 4 方向");
		static const char* kKeys[LaunchChecklist::kThrusterDirCount] = { "W", "A", "S", "D" };
		for (int i = 0; i < LaunchChecklist::kThrusterDirCount; ++i) {
			const float x = kWidgetX + static_cast<float>(i) * kThrusterLampPitch;
			const float by = y + kLampOffsetY;
			if (c.IsThrusterHeld(i)) {
				Rect(x, by, kThrusterLampSize, kThrusterLampSize, { 1.0f, 1.0f, 1.0f, 1.0f });
				TextCentered(kKeys[i], x + kThrusterLampTextX, by + kThrusterLampTextY, 1.0f, kInk);
			} else if (c.IsThrusterDone(i)) {
				Rect(x, by, kThrusterLampSize, kThrusterLampSize, WithAlpha(green_, kThrusterDoneAlpha));
				Frame(x, by, kThrusterLampSize, kThrusterLampSize, kThickFrameThickness, green_);
				TextCentered(kKeys[i], x + kThrusterLampTextX, by + kThrusterLampTextY, 1.0f, green_);
			} else {
				Frame(x, by, kThrusterLampSize, kThrusterLampSize, kThinFrameThickness, yellow_);
				TextCentered(kKeys[i], x + kThrusterLampTextX, by + kThrusterLampTextY, 1.0f, yellow_);
			}
		}
		DrawStatusBox(statusX, statusY(2), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Thruster),
			c.TimeSinceCleared(LaunchChecklist::Item::Thruster));
	}

	// 3. 武装（射撃の回数）
	{
		const float y = rowY(3);
		rowFrame(3, "03", "WEAPON", "左クリック / RT で射撃");
		// L-CLICK を大きくし、撃った数のランプはその右に横に並べる（2026-10-19 ユーザー：縦に並べると小さくて読めない）
		constexpr float kHintSize = 0.8f;
		const float hintY = y + kLampOffsetY;
		const float hintW = keyHint("L-CLICK", kWidgetX, hintY, kHintSize);
		const float lampSize = kGlyphHeight * kHintSize + kKeyHintPaddingY;
		for (int i = 0; i < c.GetWeaponShots(); ++i) {
			const float x = kWidgetX + hintW + kShotLampGap + static_cast<float>(i) * (lampSize + kShotLampSpacing);
			if (i < c.GetWeaponCount()) Rect(x, hintY, lampSize, lampSize, green_);
			else Frame(x, hintY, lampSize, lampSize, kThickFrameThickness, dimGreen_);
		}
		DrawStatusBox(statusX, statusY(3), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Weapon),
			c.TimeSinceCleared(LaunchChecklist::Item::Weapon));
	}

	// 下の段：状態
	const float fy = kFooterTop;
	const float footerH = kHeight - kPad * 0.5f - fy;
	switch (status_) {
	case Status::Checking: {
		Text("SYSTEM CHECK", kPad, fy + kFooterTextOffsetY, kFooterSize, yellow_);
		char count[32];
		std::snprintf(count, sizeof(count), "%d / %d", c.ClearedCount(), LaunchChecklist::kItemCount);
		const float w = TextRenderer::GetInstance()->MeasureWidth(count, kFooterSize);
		Text(count, right - w, fy + kFooterTextOffsetY, kFooterSize, yellow_);
		Text("LAUNCH UNABLE", kPad, fy + kFooterTextOffsetY + kGlyphHeight * kFooterSize + kFooterSubLineGap, kFooterSubScale, red_);
		break;
	}
	case Status::PowerUp: {
		const float on = CockpitReveal::Flicker(statusTime_, kFooterFlickerSeconds);
		TextCentered("COURSE CLEAR", kWidth * 0.5f, fy + kCourseClearOffsetY, kFooterSize * kCourseClearScale, WithAlpha(green_, on));
		// 点け直しの進み（流れる点）
		const int dots = static_cast<int>(std::fmod(statusTime_ * kDotsPerSecond, kDotCycle));
		std::string power = "CATAPULT POWER-UP";
		power.append(static_cast<size_t>(dots), '.');
		Text(power, kWidth * 0.5f - kPowerUpOffsetX, fy + kCourseClearOffsetY + kGlyphHeight * kFooterSize * kCourseClearScale + kPowerUpGap, kFooterSubScale, WithAlpha(dimGreen_, on));
		break;
	}
	case Status::Charging:
	case Status::Ready:
	case Status::Launch: {
		// LAUNCH READY と、カタパルトの充電（2026-10-19 ユーザー：ゲージは出さず、済んだら CHARGED と出すだけ）。
		// SPACE の長押しはタコメーターの方に出す
		const float on = status_ == Status::Charging ? CockpitReveal::Flicker(statusTime_, kFooterFlickerSeconds) : 1.0f;
		Rect(kPad, fy + kFooterBoxInsetY, kWidth * 0.5f - kPad, footerH - kFooterBoxHeightTrim, WithAlpha(green_, kReadyBoxAlpha * on));
		TextCentered("LAUNCH READY", kPad + (kWidth * 0.5f - kPad) * 0.5f, fy + (footerH - kGlyphHeight * kFooterSize) * 0.5f, kFooterSize,
			WithAlpha(kInk, on));
		const float boxX = kWidth * 0.5f + kChargeBoxGap;
		const float boxW = right - boxX;
		const float boxH = footerH - kFooterBoxHeightTrim;
		const float textSize = kFooterSize * kChargeTextScale;
		const float textY = fy + (footerH - kGlyphHeight * textSize) * 0.5f;
		if (status_ == Status::Charging) {
			// 充電中：黄の枠と文字が点滅し、点が流れる
			const float blink = Blink(time_, kChargeBlinkHz) * on;
			Frame(boxX, fy + kFooterBoxInsetY, boxW, boxH, kThickFrameThickness, WithAlpha(yellow_, blink));
			const int dots = static_cast<int>(std::fmod(statusTime_ * kDotsPerSecond, kDotCycle));
			std::string charging = "CHARGING";
			charging.append(static_cast<size_t>(dots), '.');
			Text(charging, boxX + boxW * 0.5f - TextRenderer::GetInstance()->MeasureWidth("CHARGING", textSize) * 0.5f, textY,
				textSize, WithAlpha(yellow_, blink));
		} else {
			// 充電済み：チラついて黄地の CHARGED に変わる
			const float lit = status_ == Status::Ready ? CockpitReveal::Flicker(statusTime_, kFooterFlickerSeconds) : 1.0f;
			Rect(boxX, fy + kFooterBoxInsetY, boxW, boxH, WithAlpha(yellow_, lit));
			TextCentered("CHARGED", boxX + boxW * 0.5f, textY, textSize, WithAlpha(kInk, lit));
		}
		break;
	}
	}
}

// ---------------------------------------------------------------- 調整値

void LaunchControlBoard::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	offsetFromEye_ = ReadVector3(node["offsetFromEye"], offsetFromEye_);
	rotationDegrees_ = ReadVector3(node["rotationDegrees"], rotationDegrees_);
	read("width", width_);
	{
		const Vector3 anchor = ReadVector3(node["panelScreenAnchor"], { panelScreenAnchor_.x, panelScreenAnchor_.y, 0.0f });
		panelScreenAnchor_ = { anchor.x, anchor.y };
	}
	read("panelDistance", panelDistance_);
	panelFaceEye_ = node["panelFaceEye"].AsBool(panelFaceEye_);
	read("panelScreenWidth", panelScreenWidth_);
	panelAngleOffsetDegrees_ = ReadVector3(node["panelAngleOffsetDegrees"], panelAngleOffsetDegrees_);
	read("emissive", emissive_);
	read("bloomScale", bloomScale_);
	read("appearSeconds", appearSeconds_);
	read("backgroundAlpha", backgroundAlpha_);
	green_ = ReadVector4(node["green"], green_);
	dimGreen_ = ReadVector4(node["dimGreen"], dimGreen_);
	yellow_ = ReadVector4(node["yellow"], yellow_);
	red_ = ReadVector4(node["red"], red_);
}

void LaunchControlBoard::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["offsetFromEye"] = MakeVector3(offsetFromEye_);
	node["rotationDegrees"] = MakeVector3(rotationDegrees_);
	write("width", width_);
	node["panelScreenAnchor"] = MakeVector3({ panelScreenAnchor_.x, panelScreenAnchor_.y, 0.0f });
	write("panelDistance", panelDistance_);
	node["panelFaceEye"] = JsonValue(panelFaceEye_);
	write("panelScreenWidth", panelScreenWidth_);
	node["panelAngleOffsetDegrees"] = MakeVector3(panelAngleOffsetDegrees_);
	write("emissive", emissive_);
	write("bloomScale", bloomScale_);
	write("appearSeconds", appearSeconds_);
	write("backgroundAlpha", backgroundAlpha_);
	node["green"] = MakeVector4(green_);
	node["dimGreen"] = MakeVector4(dimGreen_);
	node["yellow"] = MakeVector4(yellow_);
	node["red"] = MakeVector4(red_);
}

void LaunchControlBoard::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults##board")) ResetToDefaults();
	ImGui::TextDisabled("1st person (floating board)");
	ImGui::DragFloat3("Offset From Eye", &offsetFromEye_.x, 0.005f);
	ImGui::DragFloat3("Rotation (deg)", &rotationDegrees_.x, 0.5f);
	ImGui::DragFloat("Width (m)", &width_, 0.005f, 0.05f, 3.0f);
	ImGui::TextDisabled("3rd person (panel mounted in catapult)");
	ImGui::DragFloat2("Panel Screen Anchor (-1..1)", &panelScreenAnchor_.x, 0.005f, -1.5f, 1.5f);
	ImGui::DragFloat("Panel Distance (m)", &panelDistance_, 0.1f, 1.0f, 200.0f);
	ImGui::Checkbox("Panel Face Eye (off = parallel to screen)", &panelFaceEye_);
	ImGui::DragFloat3("Panel Angle Offset (deg)", &panelAngleOffsetDegrees_.x, 0.5f);
	ImGui::DragFloat("Panel Screen Width (ratio)", &panelScreenWidth_, 0.005f, 0.05f, 1.0f);
	ImGui::TextDisabled("Common");
	ImGui::DragFloat("Emissive", &emissive_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Appear (s)", &appearSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Background Alpha", &backgroundAlpha_, 0.01f, 0.0f, 1.0f);
	ImGui::ColorEdit4("Green", &green_.x);
	ImGui::ColorEdit4("Dim Green", &dimGreen_.x);
	ImGui::ColorEdit4("Yellow", &yellow_.x);
	ImGui::ColorEdit4("Red", &red_.x);
	if (texture_) {
		ImGui::Image((ImTextureID)TextureManager::GetInstance()->GetSrvHandleGPU(kTextureName).ptr, ImVec2(256.0f, 180.0f));
	}
#endif // _DEBUG
}

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

	/// <summary>調整値を既定にする</summary>
	LaunchTachometer();
	~LaunchTachometer();
	/// <summary>調整値（色・セグメント・板の置き場所）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>中身を描く RenderTexture を作って外部テクスチャとして登録し、一人称と三人称の板のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>外部テクスチャの登録と RenderTexture・板を解放する（共有の板のモデルの発光は LaunchControlBoard::Finalize が戻す）</summary>
	void Finalize();

	/// <summary>表示の状態を設定する</summary>
	/// <param name="thrust">出力 0〜1</param>
	/// <param name="rpm">回転数</param>
	/// <param name="maxRpm">最大の回転数</param>
	/// <param name="redZone">赤の始まり（出力 0〜1）</param>
	/// <param name="mode">表示の段階</param>
	/// <param name="modeTime">今の Mode に入ってからの秒 [秒]</param>
	/// <param name="appearTime">出始めてからの秒 [秒]（負で出さない）</param>
	void SetState(float thrust, float rpm, float maxRpm, float redZone, Mode mode, float modeTime, float appearTime);
	/// <summary>一人称の板（座った目から）と三人称のパネル（カメラの画面の位置から）を置く。LaunchControlBoard::Update と同じ引数</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="mechOrigin">機体の原点（ワールド）</param>
	/// <param name="thirdPersonEye">三人称のカメラの目（前進 0 のとき、ワールド）</param>
	/// <param name="thirdPersonTarget">三人称のカメラの注視点（前進 0 のとき、ワールド）</param>
	/// <param name="fovY">三人称のカメラの縦の画角 [ラジアン]</param>
	/// <param name="aspect">三人称のカメラの縦横比（幅 / 高さ）</param>
	void Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye, const Vector3& thirdPersonTarget,
		float fovY, float aspect);

	/// <summary>中身を RenderTexture に描く。シーンの RT を描き始める前に呼ぶ</summary>
	/// <param name="commandList">描画のコマンドリスト</param>
	void RenderMeterTexture(ID3D12GraphicsCommandList* commandList);
	/// <summary>メーターの絵を貼ってから、一人称なら浮く板、三人称ならパネルを描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="firstPerson">一人称なら true</param>
	void Draw(DirectXCore* dxCore, bool firstPerson);
	/// <summary>発光パスで板を描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="firstPerson">一人称なら true</param>
	void DrawBloom(DirectXCore* dxCore, bool firstPerson);
	/// <summary>メーターを出しているかを返す</summary>
	/// <returns>出していれば true</returns>
	bool IsShown() const { return appearTime_ >= 0.0f; }

	/// <summary>調整値（StagePlay.json の "stageStart" → "tachometer"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

	static constexpr float kSize = 600.0f;

private:
	/// <summary>メーターの中身（LED のセグメント・針・数字・状態）を積む</summary>
	void DrawContents();
	/// <summary>文字を積む</summary>
	/// <param name="text">文字列</param>
	/// <param name="x">左端 [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="size">文字の高さ [px]</param>
	/// <param name="color">色</param>
	void Text(std::string_view text, float x, float y, float size, const Vector4& color);
	/// <summary>文字を横の中央にそろえて積む</summary>
	/// <param name="text">文字列</param>
	/// <param name="cx">中央の x [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="size">文字の高さ [px]</param>
	/// <param name="color">色</param>
	void TextCentered(std::string_view text, float cx, float y, float size, const Vector4& color);
	/// <summary>塗りつぶしの四角を積む</summary>
	/// <param name="x">左端 [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="w">幅 [px]</param>
	/// <param name="h">高さ [px]</param>
	/// <param name="color">色</param>
	void Rect(float x, float y, float w, float h, const Vector4& color);
	/// <summary>四角の枠線（4 辺）を積む</summary>
	/// <param name="x">左端 [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="w">幅 [px]</param>
	/// <param name="h">高さ [px]</param>
	/// <param name="thickness">線の太さ [px]</param>
	/// <param name="color">色</param>
	void Frame(float x, float y, float w, float h, float thickness, const Vector4& color);
	/// <summary>板のマテリアルをライティングなし・発光ありにする（GPU の準備ができてから）</summary>
	void ApplyMaterial();
	/// <summary>板のモデル（確認パネルと共有）にメーターの絵を貼る（GPU の準備ができてから）</summary>
	/// <param name="target">貼る板</param>
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

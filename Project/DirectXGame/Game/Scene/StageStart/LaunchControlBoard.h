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
class LaunchChecklist;
struct ID3D12GraphicsCommandList;

/// <summary>
/// 機体確認の表示板 LAUNCH CONTROL（16_StageStart.md §3.1、F4）。緑のネオンに黄・赤の差し色、CLEAR は白地。
/// 行：POWER CABLE（操作しない。黄の ON-LINE、F5 の切り離しで赤の PURGE）／照準／スラスター（W A S D）／武装。
/// 下の段に状態：SYSTEM CHECK → COURSE CLEAR（カタパルトの点け直し中）→ LAUNCH READY と [SPACE] BOOST。
/// 中身は RenderTexture（kWidth × kHeight）に 1 回だけ描き、同じ絵を 2 枚の板ポリに貼る（2026-10-19 ユーザー決定）：
///   一人称 … コクピットの中の宙に浮く板（座ったパイロットの目からの相対。HatchSign と同じ作り）
///   三人称 … カタパルトの中に据え付けたパネル。置き場所は「三人称のカメラ（前進 0）から見た画面の位置と距離」で決める
///             （既定は画面の左上）。向きは「カメラの目の位置へ向けて yaw を少しずらす」（据え付けた板らしい斜め。ユーザーが選んだ見え方）
///             と「カメラと同じ向き＝画面と平行」（歪まない）を panelFaceEye で切り替える。目へ向けると画面の端では台形に広がるので、
///             中心は少し内側に置く。カメラは固定なのでパネルは動かない。画面の UI にすると機体と重なったのでやめた（2026-10-19 ユーザー）
/// 板は文字だけ光らせる（Material::emissive の w = 1）。調整値は StagePlay.json の "stageStart" → "board"。
/// </summary>
class LaunchControlBoard {
public:
	enum class Status {
		Checking,   // 項目を確認中
		PowerUp,    // 全部 CLEAR：カタパルトのネオンを点け直している
		Charging,   // 射出口まで点いた：カタパルトを充電中（F5）
		Ready,      // 充電済み（CHARGED）：SPACE の長押しはタコメーター（LaunchTachometer）に出す
		Launch,     // 発進（F5）
	};

	/// <summary>調整値を既定にする</summary>
	LaunchControlBoard();
	~LaunchControlBoard();
	/// <summary>調整値（色・板の置き場所・大きさ・発光）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>中身を描く RenderTexture を作って外部テクスチャとして登録し、一人称と三人称の板のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>板の発光を戻し（モデルはタイトルのロゴ・HatchSign と共有）、外部テクスチャの登録と RenderTexture・板を解放する</summary>
	void Finalize();

	/// <summary>表示の状態を設定する</summary>
	/// <param name="checklist">機体確認の判定（項目の表示に使う）</param>
	/// <param name="status">下の段の状態</param>
	/// <param name="statusTime">今の状態に入ってからの秒 [秒]</param>
	/// <param name="appearTime">表示板が出始めてからの秒 [秒]（負で出さない）</param>
	/// <param name="cablePurged">給電ケーブルを切り離したか（POWER CABLE を PURGE にする）</param>
	void SetState(const LaunchChecklist* checklist, Status status, float statusTime, float appearTime, bool cablePurged);
	/// <summary>一人称の板（座ったパイロットの目からの相対）と三人称のパネル（カメラの画面の位置から）を置く</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="mechOrigin">機体の原点（ワールド）</param>
	/// <param name="thirdPersonEye">三人称のカメラの目（前進 0 のとき、ワールド）</param>
	/// <param name="thirdPersonTarget">三人称のカメラの注視点（前進 0 のとき、ワールド）</param>
	/// <param name="fovY">三人称のカメラの縦の画角 [ラジアン]</param>
	/// <param name="aspect">三人称のカメラの縦横比（幅 / 高さ）</param>
	void Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye, const Vector3& thirdPersonTarget,
		float fovY, float aspect);

	/// <summary>中身を RenderTexture に描く。シーンの RT を描き始める前に呼ぶ（RTV とビューポートを切り替える）</summary>
	/// <param name="commandList">描画のコマンドリスト</param>
	void RenderBoardTexture(ID3D12GraphicsCommandList* commandList);
	/// <summary>一人称なら宙に浮く板、三人称なら立てたパネルを描く（Object3DManager::DrawSetting の後。半透明なので最後に）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="firstPerson">一人称なら true</param>
	void Draw(DirectXCore* dxCore, bool firstPerson);
	/// <summary>発光パスで板の文字の所を描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="firstPerson">一人称なら true</param>
	void DrawBloom(DirectXCore* dxCore, bool firstPerson);

	/// <summary>表示板を出しているかを返す</summary>
	/// <returns>出していれば true</returns>
	bool IsShown() const { return checklist_ && appearTime_ >= 0.0f; }

	/// <summary>
	/// 三人称のパネルを、カメラの画面の anchor に見える向きへ distance 進んだ所に置く。
	/// 板は logo_plane.mesh（2 × 1、表が −Z）。タコメーター（LaunchTachometer）も使う
	/// </summary>
	/// <param name="panel">置く板</param>
	/// <param name="anchor">画面の位置（端が ±1、上が +）</param>
	/// <param name="distance">カメラからの距離 [m]</param>
	/// <param name="faceEye">true で目の位置へ向ける（false で画面と平行）</param>
	/// <param name="angleOffsetDegrees">そこからずらす角度 [度]（ピッチ・ヨー・ロール）</param>
	/// <param name="screenWidth">幅（画面の幅の何倍か）</param>
	/// <param name="heightPerWidth">高さ ÷ 幅</param>
	/// <param name="eye">カメラの目（ワールド）</param>
	/// <param name="target">カメラの注視点（ワールド）</param>
	/// <param name="fovY">カメラの縦の画角 [ラジアン]</param>
	/// <param name="aspect">カメラの縦横比（幅 / 高さ）</param>
	static void PlaceOnScreen(Object3DInstance& panel, const Vector2& anchor, float distance, bool faceEye,
		const Vector3& angleOffsetDegrees, float screenWidth, float heightPerWidth,
		const Vector3& eye, const Vector3& target, float fovY, float aspect);

	/// <summary>調整値（StagePlay.json の "stageStart" → "board"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

	static constexpr float kWidth = 940.0f;    // 2026-10-19 項目名とゲージの列のあいだの空きを詰めた（1024 → 940）
	static constexpr float kHeight = 720.0f;   // 2026-10-19 ユーザー指示で縦を少し大きく（640 → 720）

private:
	// RenderTexture のピクセルで積む
	/// <summary>表示板の中身（見出し・各行・下の段の状態）を積む</summary>
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
	/// <summary>行の右の状態の枠を積む（CLEAR になった瞬間は蛍光灯のようにチラついて白地に変わる）</summary>
	/// <param name="x">左端 [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="w">幅 [px]</param>
	/// <param name="h">高さ [px]</param>
	/// <param name="cleared">CLEAR になったか</param>
	/// <param name="sinceCleared">CLEAR になってからの秒 [秒]</param>
	void DrawStatusBox(float x, float y, float w, float h, bool cleared, float sinceCleared);
	/// <summary>板のマテリアルを表示のテクスチャ・ライティングなし・文字の所だけ発光にする（GPU の準備ができてから）</summary>
	void ApplyMaterial();

	float alpha_ = 1.0f;   // 出るときのチラつき（全部の色の不透明度に掛ける）

	std::unique_ptr<RenderTexture> texture_;
	std::unique_ptr<Object3DInstance> board_;   // 一人称：宙に浮く板
	std::unique_ptr<Object3DInstance> panel_;   // 三人称：カタパルトに立てたパネル
	const LaunchChecklist* checklist_ = nullptr;
	Status status_ = Status::Checking;
	float statusTime_ = 0.0f;
	float appearTime_ = -1.0f;
	bool cablePurged_ = false;
	float time_ = 0.0f;

	// 調整値
	Vector3 offsetFromEye_ = { -0.24f, -0.15f, 0.62f }; // 一人称：座った目からの板の位置（エンジン座標、正面 +Z）。2026-10-19 左へ（右にタコメーター）
	Vector3 rotationDegrees_ = { 10.0f, -14.0f, 0.0f };    // 一人称：目の方へ少し向ける
	float width_ = 0.62f;                                 // 一人称：板の幅 [m]（高さは kHeight / kWidth 倍）
	Vector2 panelScreenAnchor_ = { -0.36f, 0.40f };      // 三人称：パネルの中心が来る画面の位置（端が ±1、上が +）。左上に余白を残す
	float panelDistance_ = 20.0f;                         // 三人称：カメラからの距離 [m]
	bool panelFaceEye_ = true;                            // 三人称：true でカメラの目の位置へ向ける、false で画面と平行
	// 三人称：上の向きからずらす角度（度。ピッチ・ヨー・ロール）
	Vector3 panelAngleOffsetDegrees_ = { 0.0f, -10.0f, 0.0f };
	float panelScreenWidth_ = 0.385f;                     // 三人称：パネルの幅（画面の幅に対する割合。距離を変えても画面の大きさは同じ）
	float emissive_ = 0.06f;                              // 板の文字に足す明るさ
	float bloomScale_ = 1.2f;                             // 板を発光 RT に書く倍率（強いと白地の箱がにじんで文字がつぶれる）
	float appearSeconds_ = 0.35f;                         // 出るときのチラつき
	float backgroundAlpha_ = 0.85f;
	Vector4 green_ = { 0.35f, 1.0f, 0.6f, 1.0f };
	Vector4 dimGreen_ = { 0.12f, 0.45f, 0.25f, 1.0f };
	Vector4 yellow_ = { 1.0f, 0.82f, 0.2f, 1.0f };
	Vector4 red_ = { 1.0f, 0.25f, 0.38f, 1.0f };
};

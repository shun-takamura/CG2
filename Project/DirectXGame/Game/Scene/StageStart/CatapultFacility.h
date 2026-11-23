#pragma once
#include "StageStart/StageStartGeometry.h"
#include "Vector3.h"
#include "Vector4.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class JsonValue;

/// <summary>
/// StagePlay のスタート演出の施設（16_StageStart.md §2・§3）。カタパルト・左右の液晶の帯・射出口の光・
/// リフトと両脚の下の固定台・縦穴の繰り返しの単位を置き、カタパルトの灯り（ポイントライト・発光）を持つ。
/// モデルは gen_catapult.py（F1）のもの、寸法は StageStartGeometry.json（手で直さない）。
///
/// 座標はカタパルトの原点（リフトが止まる位置の中心、床 y=0、機体は +Z へ）からの相対で、
/// ワールドの離れた所（origin）に置く。縦穴の単位は HubLiftDescent（③OUT）と同じ「決まった枠」の方式で、
/// 単位 k の下の端 y = shaftBaseY + unitHeight·k（k ≥ 0）の枠のうちリフトの近くだけに置く。
/// HUB の枠（y = −topCollarDepth − unitHeight·(k+1)）とは位相（StageStartHandoff::shaftPhase）でつなぐ。
/// 調整値は StagePlay.json の "stageStart" → "facility"。
/// </summary>
class CatapultFacility {
public:
	/// <summary>ポイントライトの枠を空にし、天井の近くに並べる灯りの既定値を決める</summary>
	CatapultFacility();
	~CatapultFacility();

	/// <summary>カタパルト・液晶の帯・射出口の光・リフト・固定台・縦穴の単位のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="geometry">寸法（StageStartGeometry.json）</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry);
	/// <summary>ポイントライトを借りる（演出を始めるとき。借りている間は何度呼んでもよい）</summary>
	void AcquireLights();
	/// <summary>ポイントライトを返し、共有のマテリアル（液晶の UV）を元に戻す（演出を終えたとき・シーンの終わり。何度呼んでもよい）</summary>
	void Finalize();

	/// <summary>カタパルトの原点を置くワールドの位置を設定する</summary>
	/// <param name="origin">ワールド位置</param>
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>カタパルトの原点のワールドの位置を返す</summary>
	/// <returns>ワールド位置</returns>
	const Vector3& GetOrigin() const { return origin_; }
	/// <summary>リフトの上面の高さを設定する</summary>
	/// <param name="liftY">カタパルトの原点からの高さ [m]（0 で停止位置）</param>
	void SetLiftY(float liftY) { liftY_ = liftY; }
	/// <summary>リフトの上面の高さを返す</summary>
	/// <returns>カタパルトの原点からの高さ [m]</returns>
	float GetLiftY() const { return liftY_; }
	/// <summary>固定台が溝に沿って前（+Z）へ走った距離を設定する（発進）</summary>
	/// <param name="travel">距離 [m]（0 で機体確認の位置）</param>
	void SetClampTravel(float travel) { clampTravel_ = travel; }
	/// <summary>射出口の光の倍率を設定する（機体が近づくと弱め、ブルームのにじみに機体が埋もれないようにする）</summary>
	/// <param name="level">exitLightScale に掛ける倍率</param>
	void SetExitLightLevel(float level) { exitLightLevel_ = level; }
	/// <summary>射出口の板を描かず、奥に空を見せているかを返す</summary>
	/// <returns>見せていれば true</returns>
	bool IsExitOpenToSky() const { return exitOpenToSky_; }
	/// <summary>カタパルトの原点からの位置をワールドへ直す</summary>
	/// <param name="local">カタパルトの原点からの位置</param>
	/// <returns>ワールド位置</returns>
	Vector3 ToWorld(const Vector3& local) const;

	/// <summary>発進の可否の見た目を「発進不可」（ネオン赤ピンク・液晶 LAUNCH UNABLE）に戻す（演出の始め）</summary>
	void ResetLaunchState();
	/// <summary>
	/// 機体確認が済んだ（F4、2026-10-19 ユーザー決定）：液晶はすぐ LAUNCH READY に替え、ネオンは一度全部消してから
	/// 機体の位置から射出口へ 1 列ずつ緑で点け直す。固定台と機体より後ろの列は最初に点く
	/// </summary>
	void BeginReadySequence();
	/// <summary>点け直しを進める（Update の前に呼ぶ）</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void AdvanceReadySequence(float deltaTime);
	/// <summary>射出口まで点き切ったかを返す（ブーストの許可を出してよい）</summary>
	/// <returns>点き切ったら true</returns>
	bool IsLaunchReady() const { return readyPhase_ == ReadyPhase::Ready; }
	/// <summary>点け直しの先頭の z を返す。電磁カタパルトの雷をネオンと一緒に伸ばすのに使う（F5）</summary>
	/// <returns>カタパルトの原点からの z [m]。消えている間は始まりの z、点き切ったら射出口、発進不可の間は負の大きな値</returns>
	float GetSweepFrontZ() const;
	/// <summary>点け直しの最中か済んだか（液晶はもう READY）を返す</summary>
	/// <returns>発進不可でなければ true</returns>
	bool IsReadySequenceStarted() const { return readyPhase_ != ReadyPhase::Unable; }

	/// <summary>部品を置き、縦穴の単位を並べ直し、灯りと発光を反映する</summary>
	/// <param name="lampLevel">灯りの強さ 0〜1（ポイントライト）</param>
	/// <param name="lcdScroll">液晶の文字を流した量 [m]（進行方向へ）</param>
	void Update(float lampLevel, float lcdScroll);
	/// <summary>施設を描く（不透明物。Object3D の DrawSetting・BindLights の後）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パスで照明の帯・ネオン・液晶・射出口の光を描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="bloomScale">発光 RT に書くときの倍率</param>
	void DrawBloom(DirectXCore* dxCore, float bloomScale);

	/// <summary>調整値（StagePlay.json の "stageStart" → "facility"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	struct Lamp {
		Vector3 position;        // カタパルトの原点から
		float intensity = 1.0f;
		float radius = 30.0f;
		float decay = 1.5f;
	};
	static constexpr uint32_t kLampCount = 4;

	enum class ReadyPhase { Unable, Sweeping, Ready };

	/// <summary>縦穴の単位を、決まった枠のうちリフトの裏から上の近くだけに置き直す</summary>
	void PlaceShaftUnits();
	/// <summary>照明の帯・ネオン・液晶・射出口の発光と液晶のスクロールを書き直す（モデルは共有なので毎フレーム）</summary>
	/// <param name="lcdScroll">液晶の文字を流した量 [m]</param>
	void ApplyEmissive(float lcdScroll);
	/// <summary>ネオンの列 / 固定台の点き具合を求める（発進不可の間は使わない）</summary>
	/// <param name="z">カタパルトの原点からの z [m]</param>
	/// <returns>0〜1</returns>
	float NeonLevelAt(float z) const;
	/// <summary>点け直しで最後の列が点き切る時刻を求める</summary>
	/// <returns>点け直しを始めてからの秒 [秒]</returns>
	float ReadySequenceSeconds() const;
	/// <summary>液晶の帯のテクスチャを差し替える（同じなら何もしない。GPU の準備前なら次のフレームで貼り直す）</summary>
	/// <param name="path">テクスチャのパス</param>
	void SetLcdTexture(const std::string& path);

	StageStartGeometry geometry_;
	Vector3 origin_ = { 0.0f, 0.0f, 0.0f };
	float liftY_ = 0.0f;
	float clampTravel_ = 0.0f;
	float exitLightLevel_ = 1.0f;
	ReadyPhase readyPhase_ = ReadyPhase::Unable;
	float readyTime_ = 0.0f;
	std::string lcdTexture_;   // 今の液晶のテクスチャ（毎フレーム貼り直さないように）

	std::unique_ptr<Object3DInstance> catapult_;
	std::unique_ptr<Object3DInstance> lcd_;
	std::unique_ptr<Object3DInstance> exitLight_;
	std::unique_ptr<Object3DInstance> lift_;
	std::array<std::unique_ptr<Object3DInstance>, 2> clamps_;
	std::vector<std::unique_ptr<Object3DInstance>> shaftUnits_;
	int shaftUnitCount_ = 0;   // このフレームに置いた単位の数（先頭から）
	std::array<uint32_t, kLampCount> lampSlots_{};

	// 調整値
	std::array<Lamp, kLampCount> lamps_{};
	Vector4 lampColor_ = { 0.85f, 0.9f, 1.0f, 1.0f };
	float unitsAboveLift_ = 40.0f;                    // リフトの上面からこの高さまで縦穴の単位を置く [m]（HUB と同じ）
	Vector3 lightEmissive_ = { 1.0f, 0.97f, 0.9f };   // 照明（Light）。縦穴の帯は HUB と同じ白 ×3
	float lightEmissiveScale_ = 3.0f;
	float exitLightScale_ = 6.0f;                     // 射出口の板
	bool exitOpenToSky_ = true;                       // 射出口の板を描かず、奥に StagePlay の空を見せる（2026-10-18 ユーザー決定）
	float exitSkyBloom_ = 0.15f;                      // 空に開けているときの穴のにじみ（板のブルームの倍率）
	Vector3 neonColor_ = { 1.0f, 0.25f, 0.45f };      // ネオン（チェック未完了＝赤ピンク。F4 で緑へ）
	float neonScale_ = 2.0f;
	// 機体確認が済んだ後の点け直し（F4）
	Vector3 readyNeonColor_ = { 0.25f, 1.0f, 0.45f };  // 緑（catapult_ibl_ready.dds と同じ色）
	float readyNeonScale_ = 2.0f;
	float neonOffColor_ = 0.06f;                       // 消えている間のネオンの板の色（白の地を暗くする）
	float neonOffSeconds_ = 0.4f;                      // 全部消えている間
	float neonSweepOriginZ_ = 0.0f;                    // ここ（機体の位置）より後ろの列は固定台と一緒に最初に点く
	float neonSweepSpeed_ = 60.0f;                     // 点いていく速さ [m/s]（射出口まで約 2.5 秒）
	float neonFlickerSeconds_ = 0.25f;                 // 1 列が蛍光灯のようにチカチカして点き切るまで
	float lcdEmissiveScale_ = 1.5f;                   // 液晶の文字（ベースカラーを掛けて文字だけ光らせる）
};

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
	CatapultFacility();
	~CatapultFacility();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry);
	/// <summary>ポイントライトを借りる（演出を始めるとき。借りている間は何度呼んでもよい）</summary>
	void AcquireLights();
	/// <summary>ポイントライトを返し、共有のマテリアル（液晶の UV）を元に戻す（演出を終えたとき・シーンの終わり。何度呼んでもよい）</summary>
	void Finalize();

	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	const Vector3& GetOrigin() const { return origin_; }
	/// <summary>リフトの上面の高さ（カタパルトの原点から。0 で停止位置）</summary>
	void SetLiftY(float liftY) { liftY_ = liftY; }
	float GetLiftY() const { return liftY_; }
	/// <summary>固定台が溝に沿って前（+Z）へ走った距離 [m]（発進。0 で機体確認の位置）</summary>
	void SetClampTravel(float travel) { clampTravel_ = travel; }
	/// <summary>射出口の光の倍率（exitLightScale に掛ける。機体が近づくと弱め、ブルームのにじみに機体が埋もれないようにする）</summary>
	void SetExitLightLevel(float level) { exitLightLevel_ = level; }
	/// <summary>射出口の板を描かず、奥に空を見せているか</summary>
	bool IsExitOpenToSky() const { return exitOpenToSky_; }
	/// <summary>カタパルトの原点からの位置をワールドへ</summary>
	Vector3 ToWorld(const Vector3& local) const;

	/// <summary>発進の可否の見た目を「発進不可」（ネオン赤ピンク・液晶 LAUNCH UNABLE）に戻す（演出の始め）</summary>
	void ResetLaunchState();
	/// <summary>
	/// 機体確認が済んだ（F4、2026-10-19 ユーザー決定）：液晶はすぐ LAUNCH READY に替え、ネオンは一度全部消してから
	/// 機体の位置から射出口へ 1 列ずつ緑で点け直す。固定台と機体より後ろの列は最初に点く
	/// </summary>
	void BeginReadySequence();
	/// <summary>点け直しを進める（Update の前に呼ぶ）</summary>
	void AdvanceReadySequence(float deltaTime);
	/// <summary>射出口まで点き切った（ブーストの許可を出してよい）</summary>
	bool IsLaunchReady() const { return readyPhase_ == ReadyPhase::Ready; }
	/// <summary>
	/// 点け直しの先頭の z（カタパルトの原点から）。消えている間は始まりの z、点き切ったら射出口。
	/// 電磁カタパルトの雷をネオンと一緒に伸ばすのに使う（F5）。発進不可の間は負の大きな値
	/// </summary>
	float GetSweepFrontZ() const;
	/// <summary>点け直しの最中か済んだ（液晶はもう READY）</summary>
	bool IsReadySequenceStarted() const { return readyPhase_ != ReadyPhase::Unable; }

	/// <summary>
	/// 灯りの強さ 0..1（ポイントライト）。lcdScroll は液晶の文字を流した量 [m]（進行方向へ）
	/// </summary>
	void Update(float lampLevel, float lcdScroll);
	/// <summary>不透明物（Object3D の DrawSetting・BindLights の後）</summary>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パス：照明の帯・ネオン・液晶・射出口の光</summary>
	void DrawBloom(DirectXCore* dxCore, float bloomScale);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
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

	void PlaceShaftUnits();
	void ApplyEmissive(float lcdScroll);
	// ネオンの列 / 固定台の点き具合 0..1（発進不可の間は使わない）
	float NeonLevelAt(float z) const;
	// 点け直しで最後の列が点き切る時刻
	float ReadySequenceSeconds() const;
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

#pragma once
#include "Vector3.h"
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

class DirectXCore;
class SRVManager;
class Object3DManager;
class Object3DInstance;
class Camera;
class JsonValue;
class EtherVesselGlass;
class EtherStrands;
class EtherPillarFlow;

/// <summary>
/// エーテルの器の全部（19_GameOverAndDifficulty.md §4、G6）。
///   - G4 のモデル 5 部品（EtherVesselGeometry.json の parts。Swing は球と一緒に Z 軸まわりに回す）
///   - G5 のガラスの球と中のエーテル（EtherVesselGlass）
///   - 天へ昇る糸（EtherStrands を使い回す）と昇る粒（PlayerEtherAscend を球の大きさに合わせて流す）＝死んで使った分（§4.5）
///   - 支柱の溝の光（EtherPillarFlow）＝泉との行き来
/// 難易度は 0..1（文字盤の I が 0、IX が 1）で受け、針の傾きと光の強さに変える。数字はどこにも出さない（§3）。
/// 調整値は GameOver.json の "vessel"。
/// </summary>
class EtherVessel {
public:
	EtherVessel();
	~EtherVessel();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager);
	void Finalize();

	/// <summary>器の原点（土台の脚の下）のワールド位置</summary>
	void SetOrigin(const Vector3& origin);
	const Vector3& GetOrigin() const { return origin_; }

	/// <summary>液体の量（体積の割合 0..1）</summary>
	void SetFill(float fill, bool immediate);
	/// <summary>難易度（0..1）。immediate=false なら針が「カチッ」と動き、光はゆっくり寄る</summary>
	void SetDifficulty01(float difficulty01, bool immediate);
	/// <summary>光の強さに掛ける倍率（もう一度を選んだとき）</summary>
	void SetGlowBoost(float boost);
	/// <summary>球から天へ糸と粒を昇らせ続ける（rateScale は糸の本数の倍率）</summary>
	void BeginRelease(float rateScale = 1.0f);
	/// <summary>出している間の糸の本数の倍率（液面の下がる速さに合わせて毎フレーム変える）</summary>
	void SetReleaseRate(float rateScale) { releaseRateScale_ = rateScale; }
	/// <summary>粒だけ先に止める（粒は寿命が長いので、液面が止まる少し前に止めると一緒に消える）</summary>
	void StopReleaseParticles() { StopEffects(); }
	void StopRelease();
	void Splash(float strength);

	EtherPillarFlow* GetPillarFlow() const { return pillarFlow_.get(); }
	Vector3 GetSphereCenter() const;
	const Vector3& GetEtherColor() const { return etherColor_; }

	void Update(float deltaTime);
	/// <summary>波のシミュレーション。シーンの Draw の最初に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>器のモデル（不透明）。Object3D の DrawSetting・ライトは呼ぶ側</summary>
	void DrawOpaque(DirectXCore* dxCore);
	/// <summary>不透明物の後：ガラスの球 → 支柱の光 → 糸（ルートシグネチャを替える）</summary>
	void DrawTransparent(Camera* camera, const std::string& environmentCubemapPath);
	void DrawBloom(Camera* camera, const std::string& environmentCubemapPath);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	/// <summary>ガラスの調整値は EtherVesselGlass.json（G5）に別に保存する</summary>
	void SaveGlassTuning() const;
	void OnImGui();

private:
	void LoadGeometry(Object3DManager* object3DManager);
	void EmitStrand();
	void StopEffects();

	DirectXCore* dxCore_ = nullptr;
	std::unique_ptr<EtherVesselGlass> glass_;
	std::unique_ptr<EtherStrands> strands_;
	std::unique_ptr<EtherPillarFlow> pillarFlow_;
	struct Part {
		std::unique_ptr<Object3DInstance> object;
		Vector3 position;   // 器の原点から
	};
	std::vector<Part> parts_;
	Object3DInstance* swing_ = nullptr;
	Vector3 origin_{};
	float iDegrees_ = 45.0f;   // 文字盤の I（と IX）の真上からの角度。EtherVesselGeometry.json の dial.iDegrees

	// 針（傾き）と光
	float difficulty01_ = 0.5f;
	float tiltFromDeg_ = 0.0f;
	float tiltToDeg_ = 0.0f;
	float tickTimer_ = -1.0f;
	float glowFrom01_ = 0.5f;
	float glowTo01_ = 0.5f;
	float glowTimer_ = -1.0f;

	// 天へ昇る
	bool releasing_ = false;
	float releaseRateScale_ = 1.0f;
	float strandAccum_ = 0.0f;   // 出す本数の端数（倍率が毎フレーム変わるので、間隔ではなく本数を貯める）
	uint64_t ascendHandle_ = 0;
	std::mt19937 random_{ 20261024u };

	// 調整値
	Vector3 etherColor_{ 0.0f, 0.641f, 1.0f };   // §4.3（2026-10-24 G5 の実機で決定）
	float tickSeconds_ = 0.8f;           // 針が新しい難易度へ動く秒
	float tickBack_ = 2.0f;              // 行き過ぎの強さ（ease-out-back の係数。0 で行き過ぎなし。1.7 で約 10%）
	float glowFadeSeconds_ = 1.0f;       // 光が新しい難易度の強さへ寄る秒
	float strandRate_ = 30.0f;           // 糸の本数 [本/s]（rateScale 1 のとき）
	float strandScale_ = 0.2f;           // 糸の大きさ（体の大きさ 1 の糸に掛ける）
	float strandSpread_ = 0.75f;         // 糸の根元を散らす半径（その高さの球の断面の半径に対して）
	float strandDepth_ = 0.1f;           // 糸の根元を液面からどれだけ下まで散らすか（球の半径に対して）
	float strandRippleStrength_ = 0.0008f; // 糸が出るたびに液面に立てる波紋（0 で無し）
	float strandRippleRadius_ = 0.015f;  // [m]
	float ascendRadiusScale_ = 0.45f;    // 昇る粒の散らばり（球の半径に対して）
	float ascendSizeScale_ = 0.4f;       // 昇る粒の大きさの倍率
	float ascendLifeScale_ = 0.5f;       // 昇る粒の寿命の倍率（液面が止まったら粒も消えるように短く）
};

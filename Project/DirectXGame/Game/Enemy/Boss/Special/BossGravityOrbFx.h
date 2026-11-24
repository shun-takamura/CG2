#pragma once
#include "Effect/EffectManager.h"  // EffectHandle
#include "Vector3.h"
#include <vector>

class JsonValue;

/// <summary>
/// ボスの必殺技 GravityOrb の演出（モンスターハンターワールドのムフェト・ジーヴァの流れ）。
/// 溜め → 強く光る小さな球を放つ（モノクロ①）→ 世界が暗くなり、青い光の柱を伴って球がゆっくり落ちる →
/// 着地で白い粒・青いリング・閃光（モノクロ②）→ 爆発と画面を覆う閃光 → 晴れると火の粉が漂う、を受け持つ。
/// 見た目そのものはエフェクトエディタの EffectDef（Resources/Json/Effects/GravityOrb_*.json）で作り、ここは出す時と場所を決める。
/// </summary>
class BossGravityOrbFx {
public:
	/// <summary>調整値（JSON の "fx" に保存）</summary>
	struct Tuning {
		float darkLightScale = 0.25f;     // 暗いときの平行光源の強さの倍率
		float darkFadeIn = 1.2f;          // 暗くなるまで [s]
		float darkFadeOut = 1.5f;         // 明るさが戻るまで [s]
		float releaseFlashAlpha = 0.55f;  // 球を放つ瞬間の白い閃光の濃さ
		float releaseFlashTime = 0.3f;    // 球を放つ瞬間の閃光が消えるまで [s]
		float impactFlashAlpha = 0.85f;   // 着地の閃光の濃さ
		float impactFlashTime = 0.45f;    // 着地の閃光が消えるまで [s]
		float whiteoutRise = 0.12f;       // 爆発で画面が白く覆われるまで [s]
		float whiteoutHold = 0.8f;        // 白く覆われたまま [s]
		float whiteoutFall = 1.0f;        // 白が晴れるまで [s]
	};

	/// <summary>小さながれきを地面から浮かせ始める（反重力）</summary>
	/// <param name="center">中心（ボスの真下の地面）</param>
	void StartDebris(const Vector3& center);
	/// <summary>浮いているがれきを止める（閃光で吹き飛ぶ）</summary>
	void StopDebris();
	/// <summary>球を溜め始める</summary>
	/// <param name="position">球の位置</param>
	void StartCharge(const Vector3& position);
	/// <summary>球を放つ（溜めを止め、落ちる球と光の柱を出し、モノクロ①と閃光）</summary>
	/// <param name="position">球の位置</param>
	void Release(const Vector3& position);
	/// <summary>溜め・落下中の球の位置を動かす</summary>
	/// <param name="position">球の位置</param>
	void SetOrbPosition(const Vector3& position);
	/// <summary>球が着地した瞬間（落ちる球と光の柱を消し、着地のエフェクトだけ出す）</summary>
	/// <param name="position">着地点</param>
	void Land(const Vector3& position);
	/// <summary>着地の小さな爆発の後（モノクロ②と白い閃光）</summary>
	void ImpactBurst();
	/// <summary>爆発して画面を白く覆う</summary>
	/// <param name="position">爆発の中心</param>
	void Explode(const Vector3& position);
	/// <summary>白が晴れた後の火の粉</summary>
	/// <param name="position">中心</param>
	void Aftermath(const Vector3& position);
	/// <summary>盾の岩が着地した土煙</summary>
	/// <param name="position">盾の岩の位置</param>
	void RockLanded(const Vector3& position);
	/// <summary>世界を暗くする／戻す（平行光源の強さ）</summary>
	/// <param name="dark">暗くするなら true</param>
	void SetDark(bool dark);
	/// <summary>出しているエフェクトを止め、明るさとモノクロを戻す（ゲーム中の中断）</summary>
	void Abort();
	/// <summary>
	/// 持っているハンドルと閃光を捨てるだけ（終了時）。EffectManager・PostEffect・LightManager は
	/// シーンより先に終了しているので触らない
	/// </summary>
	void Discard();

	/// <summary>明るさと閃光を進める</summary>
	/// <param name="dt">経過時間 [s]（実時間）</param>
	void Update(float dt);
	/// <summary>画面を覆う白い閃光を描く（スプライトの描画の中で呼ぶ）</summary>
	/// <param name="screenW">画面の幅</param>
	/// <param name="screenH">画面の高さ</param>
	void DrawOverlay(float screenW, float screenH) const;

	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">"fx" のノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">"fx" のノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui</summary>
	/// <param name="changed">値を変えたら true にする</param>
	void OnImGui(bool& changed);

	// エフェクトの名前（エフェクトエディタで同じ名前の物を作れば差し替わる）
	static constexpr const char* kFxDebris = "GravityOrb_Debris";
	static constexpr const char* kFxCharge = "GravityOrb_Charge";
	static constexpr const char* kFxOrb = "GravityOrb_Orb";
	static constexpr const char* kFxPillar = "GravityOrb_Pillar";
	static constexpr const char* kFxImpact = "GravityOrb_Impact";
	static constexpr const char* kFxExplosion = "GravityOrb_Explosion";
	static constexpr const char* kFxEmbers = "GravityOrb_Embers";
	static constexpr const char* kFxRockLand = "GravityOrb_RockLand";
	// モノクロの並びの名前（Monochrome.json に無ければコードの既定の並びを使う）
	static constexpr const char* kMonoRelease = "GravityOrb_Release";
	static constexpr const char* kMonoImpact = "GravityOrb_Impact";

private:
	// 画面を覆う白（立ち上がり → とどまる → 消える）
	struct Flash {
		float peak = 1.0f;
		float rise = 0.0f;
		float hold = 0.0f;
		float fall = 0.1f;
		float time = 0.0f;
	};

	/// <summary>エフェクトを出す（定義が無ければ何もしない）</summary>
	EffectHandle PlayFx(const char* name, const Vector3& position) const;
	/// <summary>ループのエフェクトを止める</summary>
	void StopFx(EffectHandle& handle) const;
	/// <summary>名前のモノクロの並びを再生する。無ければ release 用／impact 用の既定の並び</summary>
	void PlayMonochrome(const char* name, bool impact) const;
	/// <summary>白い閃光を足す</summary>
	void AddFlash(float peak, float rise, float hold, float fall);
	float FlashAlpha(const Flash& flash) const;

	Tuning tuning_;
	EffectHandle debris_ = kInvalidEffectHandle;
	EffectHandle charge_ = kInvalidEffectHandle;
	EffectHandle orb_ = kInvalidEffectHandle;
	EffectHandle pillar_ = kInvalidEffectHandle;
	std::vector<Flash> flashes_;

	// 平行光源の明るさ（暗くし始めたときの値を覚えて戻す）
	bool dark_ = false;
	bool lightCaptured_ = false;
	float baseLightIntensity_ = 1.0f;
	float lightScale_ = 1.0f;
};

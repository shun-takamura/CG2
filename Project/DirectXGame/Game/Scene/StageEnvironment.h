#pragma once
#include "Vector3.h"
#include "Vector4.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Skybox;
class JsonValue;
class Object3DManager;
class CloudLayer;
class CloudRaymarcher;
class DirectXCore;
class SRVManager;
class Camera;
class RenderTexture;

/// <summary>
/// ステージを時間で区切った「環境セクション」1件分の定義。
/// 空・平行光源・フォグをまとめて持ち、道中の経過秒に応じて切り替わる。
/// fog* は Phase B（距離フォグのシェーダ実装）で使う。今は保持と補間のみでシェーダ未接続。
/// </summary>
struct StageSection {
	std::string name = "Section";
	float       startSec = 0.0f;   // このセクションが始まるステージ経過秒
	float       blendSec = 3.0f;   // 「このセクションの開始境界」を跨ぐのにかける秒

	std::string skyboxPath;        // 空文字なら Cubemap は切り替えず前のセクションを引き継ぐ
	Vector4     skyboxTint{ 1.0f, 1.0f, 1.0f, 1.0f };

	Vector3 lightDir{ 0.3f, -1.0f, 0.4f };
	Vector4 lightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	float   lightIntensity = 1.0f;

	Vector4 fogColor{ 0.5f, 0.6f, 0.7f, 1.0f };
	float   fogNear = 50.0f;
	float   fogFar = 800.0f;
	float   fogDensity = 1.0f;
};

/// <summary>セクション間を補間した「今フレームの環境値」。</summary>
struct StageEnvValues {
	Vector4 skyTint{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector3 lightDir{ 0.3f, -1.0f, 0.4f };
	Vector4 lightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	float   lightIntensity = 1.0f;
	Vector4 fogColor{ 0.5f, 0.6f, 0.7f, 1.0f };
	float   fogNear = 50.0f;
	float   fogFar = 800.0f;
	float   fogDensity = 1.0f;
};

/// <summary>
/// 道中（Rail フェーズ）の環境演出を時間で駆動するパート。
/// RailStagePart / BossStagePart と同じ切り出し方だが、必要なのは Skybox と
/// LightManager シングルトンだけなので host インターフェースは持たない。
///
/// 補間方針: セクション間を丸ごと lerp すると常に中間色になりセクションの個性が消えるため、
/// 「境界の前後 blendSec/2 の窓の中だけ遷移し、外側は素の値を維持する」方式を採る。
/// Skybox の BlendTo も同じ窓の先頭で同じ尺で発火させるので、空・光・フォグの足並みが揃う。
///
/// セクションが 1 件も無い場合は何も適用しない（＝従来どおりの挙動）。
/// </summary>
class StageEnvironment {
public:
	/// <param name="skybox">着色/Cubemap 差し替え先。</param>
	/// <param name="object3DManager">距離フォグ（b6）の供給先。null 可。</param>
	/// <param name="onCubemapChanged">
	/// Cubemap が切り替わった時に呼ばれる。環境マップ（Object3DManager::SetEnvironmentTexture）を
	/// 追従させるために使う。不要なら空で良い。
	/// </param>
	void Initialize(Skybox* skybox, Object3DManager* object3DManager,
		std::function<void(const std::string&)> onCubemapChanged = {});

	/// <summary>
	/// 遠景の雲（タイトルと同じ CloudLayer）を作り、Skybox と Object3DManager に挿す。
	/// Initialize の後・Reset の前に呼ぶ（Reset でセクションの光の向き＝太陽の向きが入る）。
	/// </summary>
	void InitializeClouds(DirectXCore* dxCore);

	/// <summary>
	/// 雲海のレイマーチ（9_CloudRendering フェーズ3）を作る。shaftPath は降下の線（縦穴の中心。レールから拾う）。
	/// パラメータは LoadFromJson（root["raymarchClouds"]）で先に読んだ値を使う
	/// </summary>
	void InitializeRaymarchClouds(DirectXCore* dxCore, SRVManager* srvManager,
		uint32_t screenWidth, uint32_t screenHeight, const std::vector<Vector3>& shaftPath);

	/// <summary>雲のスクロールを進める。フェーズに関係なく毎フレーム、Skybox の Update の前に呼ぶ</summary>
	void UpdateClouds(float deltaTime, const Vector3& eyePosition);

	/// <summary>雲海を描いてシーンに重ねる。不透明物・水面の後、Primitive の前</summary>
	void DrawRaymarchClouds(const Camera& camera, RenderTexture* sceneTarget);

	/// <summary>雲海（InitializeRaymarchClouds 前でも JSON の値は持つ）。L5 は GetParams().opacity / enabled で消す</summary>
	CloudRaymarcher* GetCloudRaymarcher() { return raymarcher_.get(); }

	/// <summary>Object3DManager はシーンをまたいで生きるので、雲を外して返す</summary>
	void Finalize();

	// unique_ptr<CloudLayer> を前方宣言のまま持つので、生成・破棄は .cpp 側で定義する
	StageEnvironment();
	~StageEnvironment();

	/// <summary>
	/// Rail フェーズ中に毎フレーム呼ぶ。区間の値が変わった時（切り替えの補間中・区間の値の編集）だけ
	/// ライト・フォグ・空の着色に書き込む。区間の途中で外から変えた値は、次の切り替えまでそのまま残る。
	/// </summary>
	/// <param name="stageSec">ステージ経過秒（RailStagePart::GetStageSeconds()）。</param>
	/// <param name="applyTint">
	/// false なら Skybox の着色だけ適用しない（必殺技の暗転 FadeColor と喧嘩させないため）。
	/// ライト/フォグ値の更新は続ける。
	/// </param>
	void Update(float stageSec, bool applyTint);

	/// <summary>
	/// 補間状態を捨てて stageSec の値を即時適用する（Seek / シーン開始時）。
	/// クロスフェードを挟むと巻き戻しでの色確認にならないので Cubemap も即差し替える。
	/// </summary>
	void Reset(float stageSec);

	bool HasSections() const { return !sections_.empty(); }
	/// <summary>遠景の雲（InitializeClouds 前は null）。大河の水面に映る空へ重ねるのに使う</summary>
	const CloudLayer* GetCloudLayer() const { return cloudLayer_.get(); }
	const StageEnvValues& GetCurrent() const { return current_; }
	/// <summary>必殺技の暗転から復帰する時の目標色（＝今のセクションの着色）。</summary>
	const Vector4& GetCurrentTint() const { return current_.skyTint; }

	/// <param name="seekTo">"Jump" ボタンでその秒へシークするためのコールバック。</param>
	void OnImGuiTuning(bool& changed, const std::function<void(float)>& seekTo);
	/// <summary>雲のパラメータ（保存はしない。値が決まったら既定値かチューニング JSON へ移す）</summary>
	void OnImGuiClouds();
	/// <summary>雲海のパラメータ（ステージの Save で root["raymarchClouds"] に保存される）</summary>
	void OnImGuiRaymarchClouds();

	void LoadFromJson(const JsonValue& root);   // root["sections"]
	void SaveToJson(JsonValue& root) const;

private:
	// stageSec が属するセクション番号（startSec <= stageSec の最後のもの）。空なら -1。
	int  FindSectionIndex(float stageSec) const;
	// 「Cubemap としてどのセクションを採用すべきか」= 遷移窓の先頭を跨いだ時点で次へ進む。
	int  FindSkyIndex(float stageSec) const;
	// 遷移窓を考慮した補間値を求める。
	void Evaluate(float stageSec, StageEnvValues& out) const;
	// index から遡って最初に見つかる非空の skyboxPath（空セクションは前を引き継ぐ）。
	const std::string* ResolveCubemapPath(int index) const;
	void ApplyValues(const StageEnvValues& v, bool applyTint);

	Skybox* skybox_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	std::function<void(const std::string&)> onCubemapChanged_;
	std::unique_ptr<CloudLayer> cloudLayer_;
	std::unique_ptr<CloudRaymarcher> raymarcher_;

	std::vector<StageSection> sections_;
	int            skyIndex_ = -1;   // 今 Skybox に載っているセクション（BlendTo の再発火防止）
	StageEnvValues current_{};

	int selected_ = -1;   // ImGui 一覧の選択行
};

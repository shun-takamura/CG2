#pragma once
#include "Vector3.h"
#include "Vector4.h"

#include <functional>
#include <string>
#include <vector>

class Skybox;
class JsonValue;
class Object3DManager;

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
	/// Rail フェーズ中に毎フレーム呼ぶ。
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
	const StageEnvValues& GetCurrent() const { return current_; }
	/// <summary>必殺技の暗転から復帰する時の目標色（＝今のセクションの着色）。</summary>
	const Vector4& GetCurrentTint() const { return current_.skyTint; }

	/// <param name="seekTo">"Jump" ボタンでその秒へシークするためのコールバック。</param>
	void OnImGuiTuning(bool& changed, const std::function<void(float)>& seekTo);

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

	std::vector<StageSection> sections_;
	int            skyIndex_ = -1;   // 今 Skybox に載っているセクション（BlendTo の再発火防止）
	StageEnvValues current_{};

	int selected_ = -1;   // ImGui 一覧の選択行
};

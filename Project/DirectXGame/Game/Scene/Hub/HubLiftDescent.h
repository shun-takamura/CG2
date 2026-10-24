#pragma once
#include "StageStart/StageStartGeometry.h"
#include "Vector3.h"
#include <array>
#include <memory>
#include <vector>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class JsonValue;

/// <summary>
/// ③ の OUT：機体の足元の八角形のリフトで縦穴を降りる（16_StageStart.md §0・§2・§4.2、F2）。
/// リフト（StartLift）・両脚の下の固定台 2 つ（CatapultClamp）・床のすぐ下の縁（StartShaftTop）・
/// 縦穴の繰り返しの単位（StartShaftUnit）を置き、降下の時間軸を持つ。モデルは gen_catapult.py（F1）のもの。
///
/// 時間軸：決定 → UI をたたむ → 待つ（リフトが切り離される揺れ）→ ゆっくり動き出して一定の速さへ → 降り続ける。
/// 縦穴の単位は床の下に 8m おきの決まった枠へ並べ、リフトの近くの枠だけに置き直すので、何周でも降りられる。
/// 寸法は StageStartGeometry.json（手で直さない）、時間軸の調整値は Hub.json の "liftDescent"。
/// 原点＝機体の足元の中央（神殿の床の高さ y=0）。
/// </summary>
class HubLiftDescent {
public:
	HubLiftDescent();
	~HubLiftDescent();

	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry);

	/// <summary>降り始める（時刻 0 ＝ステージを決めた瞬間）</summary>
	void Start();
	/// <summary>床の高さに戻す</summary>
	void Reset();
	void Update(float deltaTime);

	bool IsPlaying() const { return playing_; }
	float GetTime() const { return previewing_ ? previewTime_ : time_; }

	/// <summary>機体（と固定台）の原点の高さ。床の高さで 0、下がると負</summary>
	float GetMechY() const { return -DistanceAt(GetTime()); }
	/// <summary>降下の速さ [m/s]（正＝下へ）</summary>
	float GetSpeed() const { return SpeedAt(GetTime()); }
	/// <summary>床に穴を開けてよいか。リフトが床の高さにある間は開けない（機体の映り込みが消えるため。16 番 §2.1）</summary>
	bool IsHoleOpen() const { return DistanceAt(GetTime()) > holeOpenDepth_; }
	/// <summary>ステージ選択の UI をたたむ進み 0..1（CockpitConsole::Draw の collapse）</summary>
	float GetUiCollapse() const;
	/// <summary>外の光の倍率。床より下へ行くほど弱める</summary>
	float GetSunScale() const;
	/// <summary>StagePlay へ差し替える時刻を過ぎた</summary>
	bool ShouldChangeScene() const { return playing_ && time_ >= changeSceneTime_; }
	/// <summary>差し替えの時刻から maxPreloadWait 秒たった（裏の先読みを待つのをやめて同期ロードで差し替える）</summary>
	bool IsPreloadWaitOver() const { return playing_ && time_ >= changeSceneTime_ + maxPreloadWait_; }
	/// <summary>リフトが切り離された瞬間（1 回だけ true）。カメラを揺らす</summary>
	bool ConsumeReleaseImpact();
	float GetReleaseShakeIntensity() const { return releaseShakeIntensity_; }
	float GetReleaseShakeSeconds() const { return releaseShakeSeconds_; }

	/// <summary>③ の切れ目で StagePlay へ渡す値（灯り・外の光はシーンが足す）</summary>
	StageStartHandoff MakeHandoff() const;

	/// <summary>不透明物（Object3D の DrawSetting・BindLights の後）。縦穴は床に穴が開いている間だけ</summary>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パス：縦穴の光の帯と固定台のネオン</summary>
	void DrawBloom(DirectXCore* dxCore, float bloomScale);

	/// <summary>固定台（神殿の鏡の床に映すため。床の上に爪が出ている）</summary>
	Object3DInstance* GetClamp(size_t index) const { return clamps_[index].get(); }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	// 時刻 t までに降りた距離 [m] と、その時の速さ（閉じた式なので ImGui でシークできる）
	float DistanceAt(float time) const;
	float SpeedAt(float time) const;
	// 位置と発光を今の時刻に合わせる
	void ApplyPose();

	StageStartGeometry geometry_;

	std::unique_ptr<Object3DInstance> lift_;
	std::array<std::unique_ptr<Object3DInstance>, 2> clamps_;
	std::unique_ptr<Object3DInstance> shaftTop_;
	std::vector<std::unique_ptr<Object3DInstance>> shaftUnits_;
	int shaftUnitCount_ = 0;   // このフレームに置いた単位の数（先頭から）

	float time_ = 0.0f;
	bool playing_ = false;
	bool releaseFired_ = false;
	bool releaseImpact_ = false;
	bool previewing_ = false;
	float previewTime_ = 0.0f;

	// 調整値（Hub.json の "liftDescent"）
	float uiCollapseSeconds_ = 0.4f;      // 決定からステージ選択の UI がたたまれるまで
	float releaseTime_ = 0.7f;            // リフトが切り離されて動き出す時刻
	float accelSeconds_ = 1.5f;           // 動き出してから一定の速さになるまで（ゆっくり動き出す）
	float descentSpeed_ = 6.0f;           // 一定の速さ [m/s]
	float changeSceneTime_ = 5.0f;        // StagePlay へ差し替える時刻（裏の先読みが終わっていなければ、終わるまで降り続ける）
	float maxPreloadWait_ = 10.0f;        // 先読みを待つのは changeSceneTime からこの秒まで。過ぎたら同期ロードで差し替える
	float holeOpenDepth_ = 0.01f;         // これだけ下がったら床に穴を開ける [m]
	float restOffsetY_ = -0.002f;         // 床の高さにある間、リフトを少し沈めて床と重ねない（上面のちらつき）
	float sunFadeDepth_ = 12.0f;          // この深さまでに外の光を shaftSunScale まで弱める [m]
	float shaftSunScale_ = 0.2f;
	float unitsAboveLift_ = 40.0f;        // リフトの上面からこの高さまで縦穴の単位を置く [m]
	float releaseShakeIntensity_ = 0.04f;
	float releaseShakeSeconds_ = 0.35f;
	Vector3 bandEmissive_ = { 1.0f, 0.97f, 0.9f };   // 縦穴の光の帯（Light）
	float bandEmissiveScale_ = 3.0f;
	Vector3 clampNeonColor_ = { 1.0f, 0.25f, 0.45f }; // 固定台のネオン（チェック未完了＝赤ピンク。F4 で緑へ）
	float clampNeonScale_ = 2.0f;
	bool drawShaft_ = true;
};

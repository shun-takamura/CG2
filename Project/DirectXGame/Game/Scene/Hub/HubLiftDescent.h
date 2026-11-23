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

	/// <summary>リフト・固定台 2 つ・床の下の縁・縦穴の単位のモデルを読み込み、床の高さに置く</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="geometry">寸法（StageStartGeometry.json）</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry);

	/// <summary>降り始める（時刻 0 ＝ステージを決めた瞬間）</summary>
	void Start();
	/// <summary>床の高さに戻す</summary>
	void Reset();
	/// <summary>時刻を進め、切り離しの瞬間を記録して、位置と発光を合わせる</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>降下中かを返す</summary>
	/// <returns>降下中なら true</returns>
	bool IsPlaying() const { return playing_; }
	/// <summary>今の時刻を返す（ImGui でシーク中はシークした時刻）</summary>
	/// <returns>時刻 [秒]</returns>
	float GetTime() const { return previewing_ ? previewTime_ : time_; }

	/// <summary>機体（と固定台）の原点の高さを返す</summary>
	/// <returns>高さ [m]（床の高さで 0、下がると負）</returns>
	float GetMechY() const { return -DistanceAt(GetTime()); }
	/// <summary>降下の速さを返す</summary>
	/// <returns>速さ [m/s]（正＝下へ）</returns>
	float GetSpeed() const { return SpeedAt(GetTime()); }
	/// <summary>床に穴を開けてよいかを返す。リフトが床の高さにある間は開けない（機体の映り込みが消えるため。16 番 §2.1）</summary>
	/// <returns>開けてよければ true</returns>
	bool IsHoleOpen() const { return DistanceAt(GetTime()) > holeOpenDepth_; }
	/// <summary>ステージ選択の UI をたたむ進みを返す（CockpitConsole::Draw の collapse）</summary>
	/// <returns>0〜1</returns>
	float GetUiCollapse() const;
	/// <summary>外の光の倍率を返す。床より下へ行くほど弱める</summary>
	/// <returns>倍率</returns>
	float GetSunScale() const;
	/// <summary>StagePlay へ差し替える時刻を過ぎたかを返す</summary>
	/// <returns>過ぎたら true</returns>
	bool ShouldChangeScene() const { return playing_ && time_ >= changeSceneTime_; }
	/// <summary>差し替えの時刻から maxPreloadWait 秒たったかを返す（裏の先読みを待つのをやめて同期ロードで差し替える）</summary>
	/// <returns>たったら true</returns>
	bool IsPreloadWaitOver() const { return playing_ && time_ >= changeSceneTime_ + maxPreloadWait_; }
	/// <summary>リフトが切り離された瞬間を一度だけ返す（カメラを揺らす）。返したら下ろす</summary>
	/// <returns>切り離された瞬間が来ていれば true</returns>
	bool ConsumeReleaseImpact();
	/// <summary>切り離しの揺れの強さを返す</summary>
	/// <returns>揺れの強さ</returns>
	float GetReleaseShakeIntensity() const { return releaseShakeIntensity_; }
	/// <summary>切り離しの揺れの長さを返す</summary>
	/// <returns>長さ [秒]</returns>
	float GetReleaseShakeSeconds() const { return releaseShakeSeconds_; }

	/// <summary>③ の切れ目で StagePlay へ渡す値（リフトの高さ・縦穴の位相・降下の速さ）を作る（灯り・外の光はシーンが足す）</summary>
	/// <returns>受け渡しの値</returns>
	StageStartHandoff MakeHandoff() const;

	/// <summary>リフト・固定台・縦穴を描く（不透明物。Object3D の DrawSetting・BindLights の後）。縦穴は床に穴が開いている間だけ</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パスで縦穴の光の帯と固定台のネオンを描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="bloomScale">発光 RT に書くときの倍率</param>
	void DrawBloom(DirectXCore* dxCore, float bloomScale);

	/// <summary>固定台を返す（神殿の鏡の床に映すため。床の上に爪が出ている）</summary>
	/// <param name="index">固定台の番号 0/1</param>
	/// <returns>固定台。Initialize 前は nullptr</returns>
	Object3DInstance* GetClamp(size_t index) const { return clamps_[index].get(); }

	/// <summary>調整値（Hub.json の "liftDescent"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値とシークの ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>時刻までに降りた距離を求める（閉じた式なので ImGui でシークできる）</summary>
	/// <param name="time">時刻 [秒]</param>
	/// <returns>距離 [m]</returns>
	float DistanceAt(float time) const;
	/// <summary>その時刻の降下の速さを求める</summary>
	/// <param name="time">時刻 [秒]</param>
	/// <returns>速さ [m/s]（正＝下へ）</returns>
	float SpeedAt(float time) const;
	/// <summary>リフト・固定台・縦穴の位置と発光を今の時刻に合わせる</summary>
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

#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "Vector3.h"
#include "IImGuiEditable.h"
#include "Components/EntityTag.h"

class DirectXCore;
class SRVManager;
class Object3DManager;
class Camera;
class CloudLayer;
class WaterSurface;
class WaterReflection;
class Object3DInstance;
class AnimatedObject3DInstance;

/// <summary>
/// 低空飛行フェーズの大河の水面（13_RiverWater.md）。WaterSurface を大河モードで持つ。
///
/// 重い初期化（PSO・ルートシグネチャ・CB）は Initialize（シーン初期化）で済ませ、
/// 川のデータ（RiverTerrain.json と川のマップ）だけを雲の中（loadStartSec）で読む。
///   - CPU フェーズ（JSON の解析・テクスチャの読み込み）は別スレッド
///   - GPU フェーズ（リソース・SRV の作成）は CPU フェーズが終わった後の Update でメインスレッド
/// 描くのは、ロード済みで、カメラが水面より上にいる時だけ（谷から見上げて天井に見えないように）。
///
/// 反射（13 番 §4）：映すタグを持つ物を毎フレーム登録し直して反射 RT に描く（消えた物を指したまま残らない）。
/// カメラが高い時は映り込みがほぼ見えないので、cutoffHeight より上ではパスごと飛ばす。
///
/// 航跡（13 番 §8）：水面の波のシミュレーションの範囲を自機について動かし、水面すれすれを速く飛ぶと
/// 通った線に沿って波源を入れる（後ろに V 字の波が立つ）。
///
/// IImGuiEditable なので Hierarchy に出て、Inspector で調整できる（シーン JSON には保存しない。コードで生成する）。
/// </summary>
class RiverWater : public IImGuiEditable {
public:
	RiverWater();
	~RiverWater() override;

	std::string GetName() const override { return "RiverWater"; }
	std::string GetTypeName() const override { return "Water"; }
	void OnImGuiInspector() override { OnImGui(); }

	/// <param name="cloudLayer">映り込む空に重ねる遠景の雲（null 可）</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
		const CloudLayer* cloudLayer);

	/// <summary>
	/// Rail フェーズ中に毎フレーム呼ぶ。loadStartSec を過ぎたらロードを始め、CPU フェーズが終わっていれば仕上げる。
	/// </summary>
	/// <param name="stageSec">ステージ経過秒（RailStagePart::GetStageSeconds()）</param>
	/// <param name="deltaTime">波の時間（World 時間）</param>
	/// <param name="playerPosition">自機のワールド位置（航跡の波源。null なら航跡を立てない）</param>
	void Update(float stageSec, float deltaTime, const Vector3* playerPosition);

	/// <summary>航跡の波のシミュレーションを進める。シーン描画の最初（シーン RT を貼る前）に呼ぶ</summary>
	void DispatchSimulation();

	/// <summary>画面の水滴の濡れ具合の目標（＝航跡の波の強さ。川のロード前は 0）。StagePlayScene が雲の分とまとめて渡す</summary>
	float GetDropletTarget() const { return state_ == LoadState::Ready ? dropletTarget_ : 0.0f; }

	/// <summary>シーク。ロード開始時刻より後へ飛んだら、その場で読み終える</summary>
	void Seek(float stageSec);

	/// <summary>
	/// 反射 RT を描く。シーン描画の最初（シーン RT を貼る前）に呼ぶ。呼んだ後は RT が反射 RT のままなので、
	/// 呼び出し側でシーン RT を貼り直すこと。
	/// </summary>
	/// <param name="objects">シーンの Object3D。映すタグを持つ物だけを拾う</param>
	/// <param name="animated">シーンのアニメーションするモデル（自機・敵など）。同じくタグで拾う</param>
	void RenderReflection(const Camera& camera,
		const std::vector<std::unique_ptr<Object3DInstance>>& objects,
		const std::vector<std::unique_ptr<AnimatedObject3DInstance>>& animated);

	/// <summary>不透明物の後・半透明物の前に呼ぶ。ルートシグネチャを替えるので後続は各自設定し直すこと</summary>
	void Draw(const Camera& camera, const std::string& skyCubemapPath);

	bool IsReady() const { return state_ == LoadState::Ready; }

	void OnImGui();

private:
	enum class LoadState {
		NotStarted, // まだ読んでいない
		LoadingCPU, // 別スレッドで CPU フェーズ中
		UploadingGPU, // 川のマップを DirectStorage へ待たずに送った。fence が終わったら仕上げて貼る
		Ready,      // 描ける
		Failed,     // 読めなかった（ログに理由）
	};

	/// <summary>別スレッドが読んだ RiverTerrain.json の中身</summary>
	struct LoadedInfo {
		bool ok = false;
		std::string riverMapPath;
		float xMin = -1000.0f;
		float xMax = 1000.0f;
		float zMin = 900.0f;
		float zMax = 2600.0f;
		float maxDepth = 10.0f;
		float waterHeight = 0.0f;
	};

	/// <summary>
	/// 自機の航跡（13 番 §8）。波のシミュレーションの範囲を自機について動かし、
	/// 前のフレームの位置からの線に沿って波源を入れる。水面に近く速いほど強い
	/// </summary>
	void UpdateWake(const Vector3* playerPosition, float deltaTime);
	/// <summary>航跡の続きを切る（シーク・ワープの後に前の位置から線を引かない）。波も消す</summary>
	void ResetWake();

	// 航跡の調整値（波の伝わり方は RippleSimulation::Settings に直接持つ）
	struct WakeTuning {
		bool enabled = true;
		// 波源を自機より進む向きの前に置く量 [m]。カメラが低く（水面から 3.5m・自機の 12m 後ろ）、
		// 自機の体の真下に見える水面は実際には自機の約 8m 前、画面の下端は自機の約 1m 後ろのため（2026-10-25 実機で 5m）
		float leadDistance = 5.0f;
		float backOffset = 12.0f;        // 範囲の中心を波源から進む向きの後ろへずらす量 [m]（航跡は後ろに伸びる）
		float maxHeight = 2.0f;          // 水面からの高さがこれ以上なら波を立てない [m]
		float fullHeight = 0.5f;         // これ以下なら最大の強さ [m]
		float minSpeed = 5.0f;           // 水平の速さがこれ以下なら波を立てない [m/s]
		float fullSpeed = 25.0f;         // これ以上なら最大の強さ [m/s]
		float radius = 0.6f;             // 1 つの波源の半径 [m]
		float strengthPerMeter = 0.2f;   // 1m 進んだときのへこみの深さ [m]（進んだ距離に比例）
		float maxStrength = 0.4f;        // 1 つの波源のへこみの上限 [m]
		float spacing = 0.25f;           // 軌跡に沿って波源を置く間隔 [m]
		float teleportDistance = 30.0f;  // 1 フレームでこれ以上動いたら線を引かない [m]
	};
	float dropletTarget_ = 0.0f; // このフレームの画面の水滴の濡れ具合の目標（＝波の強さ。UpdateWake が決める）
	WakeTuning wake_;
	bool wakeHasPrev_ = false;
	Vector3 wakePrevPosition_{};
	Vector3 wakeForward_{ 0.0f, 0.0f, 1.0f }; // 最後に分かった水平の進む向き
	// ImGui の確認用（最後のフレームの値）
	float wakeLastHeight_ = 0.0f;
	float wakeLastSpeed_ = 0.0f;
	float wakeLastFactor_ = 0.0f;

	void StartLoad();
	/// <summary>CPU フェーズの後：川のマップを GPU へ待たずに送る（FS 経路はその場で貼る）</summary>
	void BeginUpload();
	/// <summary>送った途中でも今すぐ読み終えて貼る（シーク用。DirectStorage を待つ）</summary>
	void FinishLoadNow();
	/// <summary>川のマップ（GPU まで済んだ物）を水面に貼る</summary>
	void ApplyRiverMap();
	void LoadTuning();
	void SaveTuning() const;

	std::unique_ptr<WaterSurface> water_;
	std::unique_ptr<WaterReflection> reflection_;
	bool reflectionActive_ = false; // このフレームに反射 RT を描いたか

	// 反射の調整値（13 番 §4.1）
	float reflectionScale_ = 0.5f;       // 反射 RT の解像度倍率（Initialize の時だけ効く）
	float reflectionFarClip_ = 2500.0f;  // LowAltitude のフォグの far。これより遠い物は霧で見えない
	float reflectionCutoffHeight_ = 100.0f; // カメラの高さ（水面から）がこれ以上なら反射のパスを飛ばす [m]
	float reflectionFadeWidth_ = 30.0f;  // 足切りの手前で映り込みを薄くする幅 [m]
	bool reflectTags_[static_cast<int>(EntityTag::Count)]{};

	// 反射の確認用（Debug の ImGui だけ）。カメラの前方の水面近くにエフェクトを出す
	Vector3 lastCameraPosition_{};
	bool testEffectAuto_ = false;
	float testEffectInterval_ = 1.5f;
	float testEffectTimer_ = 0.0f;
	float testEffectAhead_ = 80.0f;  // カメラから +Z 方向への距離 [m]
	float testEffectHeight_ = 3.0f;  // 水面からの高さ [m]
	// 最後に出した位置と結果（出ているか確かめる用。位置には一定時間、軸の線を出す）
	Vector3 testEffectLastPosition_{};
	uint64_t testEffectLastHandle_ = 0;
	float testEffectMarkerTimer_ = 0.0f;
	void PlayTestEffect();

	LoadState state_ = LoadState::NotStarted;
	std::thread loadThread_;
	std::atomic<bool> cpuDone_{ false };
	LoadedInfo loaded_; // 別スレッドが書き、cpuDone_ が立った後にメインスレッドが読む
	uint64_t uploadFence_ = 0; // UploadingGPU の間：DStorageManager::SubmitWithFence の値

	float loadStartSec_ = 51.0f; // 雲に入る時刻（11 番 §3.0 のストリーミングの窓の先頭）
	float waterHeight_ = 0.0f;
};

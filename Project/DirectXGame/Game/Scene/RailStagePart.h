#pragma once
#include "Vector3.h"
#include "Wave/WaveDef.h"
#include "TimeGroup.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Camera;
class SplineCurveActor;
class CameraRotKey;
class RailCameraController;
class RailAimController;
class IImGuiEditable;
class EnemyController;
class JsonValue;

/// <summary>
/// RailStagePart が GameScene / StagePlayScene に対して必要とする操作だけを切り出した
/// narrow interface。StagePlayScene が private override で実装する（呼び出しは常にこの
/// 基底ポインタ経由になるため、StagePlayScene の公開APIは増えない）。
///
/// movingEnemies_ / enemyControllers_ / dynamicSplines_ は GameScene が protected で
/// 直接保持しているため、RailStagePart から扱うにはそれぞれ専用の narrow な操作
/// （RegisterStationaryMovingEnemy / LinkEnemyController / ForEachMovingEnemy /
/// TriggerRetreatForWaveEntry / FindDynamicSplineByName / GetDynamicSplines）を介す。
/// </summary>
class IRailStageHost {
public:
	virtual ~IRailStageHost() = default;

	virtual IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName,
		SplineCurveActor* spline, float speed, bool removeAtEnd,
		float initialT, int waveEntryIndex) = 0;
	virtual IImGuiEditable* SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) = 0;
	virtual void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) = 0;
	virtual void UpdateMovingEnemies(float dt) = 0;
	virtual void UpdateEnemyControllers(float dt, IImGuiEditable* player, float stageTimeSec) = 0;
	virtual void SweepDeadEntities() = 0;
	virtual void ResetDodgeState() = 0;
	virtual IImGuiEditable* GetPlayer() const = 0;
	virtual float GetTimeScale(TimeGroup g) const = 0;
	virtual void SetTimeScale(TimeGroup g, float scale) = 0;

	// movingEnemies_ の中から (entity, waveEntryIndex) を列挙する（キー記録用）。
	virtual void ForEachMovingEnemy(const std::function<void(IImGuiEditable* entity, int waveEntryIndex)>& fn) = 0;
	// カメラ相対（ScreenHover/Static）敵を movingEnemies_ へ登録する（spline=null/speed=0固定）。
	virtual void RegisterStationaryMovingEnemy(IImGuiEditable* entity, int waveEntryIndex) = 0;
	// 対応する movingEnemies_ エントリへコントローラを紐付ける。
	virtual void LinkEnemyController(IImGuiEditable* entity, EnemyController* ctrl) = 0;
	// waveEntryIndex_ が一致するコントローラへ退避を指示する。
	virtual void TriggerRetreatForWaveEntry(int waveEntryIndex) = 0;
	// 動的スプラインを名前で検索する（GameScene::FindDynamicSplineByName の委譲）。
	virtual SplineCurveActor* FindDynamicSplineByName(const std::string& name) = 0;
	// Wave Editor のスプライン選択コンボ用。
	virtual const std::vector<std::unique_ptr<SplineCurveActor>>& GetDynamicSplines() const = 0;

	// Seek() 用：既存 STG エンティティ（弾/近接判定/敵/コントローラ）を一括破棄する。
	// 中身は StagePlayScene が実装（現行 Seek() の該当コンテナクリア処理をそのまま移す）。
	virtual void ClearWaveRuntimeState() = 0;
};

/// <summary>
/// STGレールシューティング専用ロジック（レールカメラ駆動・Wave/敵スポーン・Seek再構築）を
/// StagePlayScene から切り出したパート。IRailStageHost 経由でのみ GameScene/StagePlayScene
/// の機能を呼ぶ（friend やアクセス修飾子の緩和は使わない）。
/// </summary>
class RailStagePart {
public:
	RailStagePart();
	~RailStagePart();

	void Initialize(IRailStageHost* host, Camera* camera);

	// true を返した = SeekMax 到達。呼び出し側はこのフレーム内で phase_ を Landing に進めること。
	// （このフレームは内部で railCamera_->Update() を呼んでいない＝カメラは直前ポーズで凍結）
	bool UpdateCamera(class InputActionMap* actions, float scaledDt, float seekMaxSec);

	// phase_==Rail のときだけ呼ぶ。Wave発火・敵コントローラ/移動更新・キル記録をまとめて行う。
	void UpdateWaveAndEnemies(float worldDt);

	void Seek(float seconds);              // 旧 StagePlayScene::Seek() の Rail/Wave 再構築部分
	float GetCameraProgressT() const;
	float GetStageSeconds() const;         // progress / speed。SeekMax比較・ImGui表示に使う

	void OnImGuiTuning(bool& changed);      // "Rail Camera" + (_DEBUG) "Wave Editor"
	void LoadFromJson(const JsonValue& root);   // root["camera"]（既存キー名を維持、データ非破壊）
	void SaveToJson(JsonValue& root) const;

	bool OnViewportPrefabDrop(const std::string& prefabName, float relX, float relY);
	Vector3 CameraOffsetToWorld(const Vector3& off) const;
	Vector3 WorldToCameraOffset(const Vector3& world) const;

private:
	IRailStageHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	std::unique_ptr<SplineCurveActor> cameraPath_;
	std::vector<std::unique_ptr<CameraRotKey>> cameraRotKeys_;
	std::unique_ptr<RailCameraController> railCamera_;
	std::unique_ptr<RailAimController> railAim_;
	bool aimAuthoring_ = false, prevAimAuthoring_ = false;
	float prevTimeScales_[static_cast<int>(TimeGroup::Count)] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float railCameraSpeed_ = 1.0f / 120.0f;

	WaveDef currentWave_;
	std::vector<bool> spawnFired_, retreatFired_;
	std::vector<float> killAtT_;
	std::string wavePath_ = "Resources/Json/Waves/stage1.json";

#ifdef _DEBUG
	bool waveEditMode_ = false; int waveSelectedEntry_ = -1; float waveDropDepth_ = 30.0f;
	void SaveWaveToDisk();
	void RebuildWaveRuntimeState();
	void DrawWaveEditorUI(bool& changed);
#endif
};

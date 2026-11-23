#pragma once
#include "Vector3.h"
#include "ObstacleDebris.h"
#include "Loot/LootDrops.h"
#include "Wave/WaveDef.h"
#include "TimeGroup.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GameSession;
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
	/// <summary>シーンをまたいで残るプレイの状態を取得する（スコア・難易度・手に入れた物など）</summary>
	/// <returns>プレイの状態</returns>
	virtual GameSession* GetGameSession() const = 0;

	/// <summary>指定プレハブから敵を1体スポーンし、指定スプラインに沿って進ませる</summary>
	/// <param name="prefabName">敵のプレハブ名</param>
	/// <param name="spline">沿わせるスプライン</param>
	/// <param name="speed">スプラインの進み [t/秒]</param>
	/// <param name="removeAtEnd">終端に着いたら破棄するなら true</param>
	/// <param name="initialT">スプラインの始めの位置（0〜1）</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	/// <returns>出した敵。出せなければ nullptr</returns>
	virtual IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName,
		SplineCurveActor* spline, float speed, bool removeAtEnd,
		float initialT, int waveEntryIndex) = 0;
	/// <summary>指定プレハブをスプラインなしで指定座標に直接スポーンする</summary>
	/// <param name="prefabName">プレハブ名</param>
	/// <param name="pos">置く位置（ワールド）</param>
	/// <returns>出した実体。出せなければ nullptr</returns>
	virtual IImGuiEditable* SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) = 0;
	/// <summary>敵コントローラを登録する</summary>
	/// <param name="ctrl">登録するコントローラ（所有権を移す）</param>
	virtual void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) = 0;
	/// <summary>スプラインに沿う敵を進める（終端に着いた敵を外す）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	virtual void UpdateMovingEnemies(float dt) = 0;
	/// <summary>敵コントローラを更新する</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="player">自機</param>
	/// <param name="stageTimeSec">ステージの経過時間 [秒]</param>
	virtual void UpdateEnemyControllers(float dt, IImGuiEditable* player, float stageTimeSec) = 0;
	/// <summary>HP が尽きた動的エンティティを死亡エフェクト付きで破棄する</summary>
	virtual void SweepDeadEntities() = 0;
	/// <summary>回避（ジャスト回避・分身カウンター）の状態を消す（Seek 用）</summary>
	virtual void ResetDodgeState() = 0;
	/// <summary>自機を取得</summary>
	/// <returns>自機（いなければ nullptr）</returns>
	virtual IImGuiEditable* GetPlayer() const = 0;
	/// <summary>時間グループの倍率を取得</summary>
	/// <param name="g">時間グループ</param>
	/// <returns>倍率</returns>
	virtual float GetTimeScale(TimeGroup g) const = 0;
	/// <summary>時間グループの倍率を変える</summary>
	/// <param name="g">時間グループ</param>
	/// <param name="scale">倍率</param>
	virtual void SetTimeScale(TimeGroup g, float scale) = 0;

	/// <summary>movingEnemies_ の中から (entity, waveEntryIndex) を列挙する（キー記録用）</summary>
	/// <param name="fn">敵と Wave のエントリ番号を受け取る関数</param>
	virtual void ForEachMovingEnemy(const std::function<void(IImGuiEditable* entity, int waveEntryIndex)>& fn) = 0;
	/// <summary>カメラ相対（ScreenHover/Static）敵を movingEnemies_ へ登録する（spline=null/speed=0固定）</summary>
	/// <param name="entity">敵</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	virtual void RegisterStationaryMovingEnemy(IImGuiEditable* entity, int waveEntryIndex) = 0;
	/// <summary>対応する movingEnemies_ エントリへコントローラを紐付ける</summary>
	/// <param name="entity">敵</param>
	/// <param name="ctrl">その敵のコントローラ</param>
	virtual void LinkEnemyController(IImGuiEditable* entity, EnemyController* ctrl) = 0;
	/// <summary>waveEntryIndex_ が一致するコントローラへ退避を指示する</summary>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	virtual void TriggerRetreatForWaveEntry(int waveEntryIndex) = 0;
	/// <summary>動的スプラインを名前で検索する（GameScene::FindDynamicSplineByName の委譲）</summary>
	/// <param name="name">スプラインの名前</param>
	/// <returns>見つかったスプライン。無ければ nullptr</returns>
	virtual SplineCurveActor* FindDynamicSplineByName(const std::string& name) = 0;
	/// <summary>動的スプラインの一覧（Wave Editor のスプライン選択コンボ用）</summary>
	/// <returns>スプラインの一覧（読むだけ）</returns>
	virtual const std::vector<std::unique_ptr<SplineCurveActor>>& GetDynamicSplines() const = 0;
	/// <summary>
	/// レールカメラの走行スプラインをシーンから取得する。CameraPathSpline タグのものが
	/// 無ければ defaultPoints を種にして新規作成する（＝シーン JSON に載り、保存すると
	/// Blender からも編集できるようになる）。所有はシーン側、戻り値は参照のみ。
	/// </summary>
	/// <param name="defaultPoints">無い時に作るスプラインの制御点</param>
	/// <returns>走行スプライン</returns>
	virtual SplineCurveActor* EnsureCameraPathSpline(const std::vector<Vector3>& defaultPoints) = 0;

	/// <summary>Seek() 用：既存 STG エンティティ（弾/近接判定/敵/コントローラ）を一括破棄する</summary>
	virtual void ClearWaveRuntimeState() = 0;

	/// <summary>谷の障害物が崩れた部品（ObstacleDebris）・落とし物を消す。中身は GameScene::DestroyDynamicEntity</summary>
	/// <param name="entity">消す実体</param>
	virtual void DestroyEntity(IImGuiEditable* entity) = 0;
};

/// <summary>
/// STGレールシューティング専用ロジック（レールカメラ駆動・Wave/敵スポーン・Seek再構築）を
/// StagePlayScene から切り出したパート。IRailStageHost 経由でのみ GameScene/StagePlayScene
/// の機能を呼ぶ（friend やアクセス修飾子の緩和は使わない）。
/// </summary>
class RailStagePart : private IObstacleDebrisHost, private ILootDropsHost {
public:
	RailStagePart();
	~RailStagePart();

	/// <summary>ホストとカメラを受け取り、障害物の崩れ・落とし物・レールカメラ・照準を用意する</summary>
	/// <param name="host">レールが頼む操作の窓口（StagePlayScene）</param>
	/// <param name="camera">レールで動かすシーンのカメラ</param>
	void Initialize(IRailStageHost* host, Camera* camera);

	/// <summary>
	/// レールカメラの走行スプラインをシーンから取り直す。
	/// dynamicSplines_ はシーンのロードで丸ごと作り直されるため、参照を持ち越すと
	/// 解放済みを指す。**LoadSceneFromJson の最後で必ず呼ぶこと**。
	/// </summary>
	void RebindCameraPath();

	/// <summary>
	/// Wave JSON が書き換わっていたら読み直して、現在時刻の状態に組み直す。
	/// Blender から敵配置を Export した時に、再起動せずその場で反映するためのもの。
	/// 自前で保存した直後は MarkWaveFileSynced() で記録を進め、自己トリガを防ぐ。
	/// </summary>
	void RefreshWaveIfChanged();

	/// <summary>現在の Wave ファイルの更新時刻を「既知」として記録する（自己トリガ抑止）。</summary>
	void MarkWaveFileSynced();

	/// <summary>Wave JSON を今すぐ読み直して現在時刻で組み直す（監視OFFでも使える手動リロード）。</summary>
	void ReloadWaveNow();

	/// <summary>
	/// レールカメラを進める（向きキーの並べ直し・照準の録画オーサリング・水面クランプも）。
	/// true を返したら呼び出し側はこのフレーム内で phase_ を Landing に進めること
	/// （このフレームは内部で railCamera_->Update() を呼んでいない＝カメラは直前ポーズで凍結）
	/// </summary>
	/// <param name="actions">アクションの入力（今は使っていない）</param>
	/// <param name="scaledDt">スケール済みの経過時間 [秒]</param>
	/// <param name="seekMaxSec">レールを終える経過秒 [秒]</param>
	/// <returns>SeekMax に着いたら true</returns>
	bool UpdateCamera(class InputActionMap* actions, float scaledDt, float seekMaxSec);

	/// <summary>phase_==Rail のときだけ呼ぶ。Wave発火・敵コントローラ/移動更新・キル記録・障害物の崩れ・落とし物をまとめて行う</summary>
	/// <param name="worldDt">世界の経過時間 [秒]</param>
	void UpdateWaveAndEnemies(float worldDt);

	/// <summary>旧 StagePlayScene::Seek() の Rail/Wave 再構築部分。カメラの進行度を秒から決め直し、STG のエンティティを捨ててその時刻の敵を出し直す</summary>
	/// <param name="seconds">飛ぶ先のステージ経過秒</param>
	void Seek(float seconds);
	/// <summary>レールカメラの進行度</summary>
	/// <returns>進行度 t（0〜1。レールカメラが無ければ -1）</returns>
	float GetCameraProgressT() const;
	/// <summary>ステージ経過秒（progress / speed）。SeekMax比較・ImGui表示に使う</summary>
	/// <returns>経過秒（レールカメラが無ければ 0）</returns>
	float GetStageSeconds() const;
	/// <summary>ステージ秒 sec のときのレール上のカメラ位置（揺れ・水面クランプ前の素の位置）</summary>
	/// <param name="sec">ステージ経過秒</param>
	/// <returns>カメラ位置（走行スプラインが無ければ原点）</returns>
	Vector3 SampleCameraPathAtSec(float sec) const;
	/// <summary>Wave で出すプレハブ名（prefab と child_prefab、重複なし）。ステージ開始時にモデルを先読みする用</summary>
	/// <returns>プレハブ名の一覧</returns>
	std::vector<std::string> CollectWavePrefabs() const;

	/// <summary>"Rail Camera" と（Debug のみ）"Wave Editor" の調整 UI</summary>
	/// <param name="changed">値を書き換えたら true にする（呼ぶ側が保存する）</param>
	void OnImGuiTuning(bool& changed);
	/// <summary>root["camera"]（速さ・水面クランプ・見下ろし角・向きキー）を読む（既存キー名を維持、データ非破壊）</summary>
	/// <param name="root">ステージのチューニング JSON のルート</param>
	void LoadFromJson(const JsonValue& root);
	/// <summary>root["camera"] へ速さ・水面クランプ・見下ろし角・向きキーを書く</summary>
	/// <param name="root">書き込み先のルート</param>
	void SaveToJson(JsonValue& root) const;

	/// <summary>
	/// 自機のコライダーの下端が下回ってはいけない Y。水しぶきは「自機の Y − 水面の Y」で判定する（L7）。
	/// </summary>
	/// <returns>水面＋余白の高さ。水面の窓（camera.waterClamp）の外では制限なし（-FLT_MAX）</returns>
	float GetPlayerFloorY() const;

	/// <summary>Wave 編集モードで、ビューポートへドロップしたプレハブを今の時刻の Wave のエントリとしてカメラ前方に足す（Debug のみ）</summary>
	/// <param name="prefabName">ドロップしたプレハブ名</param>
	/// <param name="relX">ビューポート内の横の位置（0〜1）</param>
	/// <param name="relY">ビューポート内の縦の位置（0〜1）</param>
	/// <returns>エントリを足したら true（編集モードでない・Release なら false）</returns>
	bool OnViewportPrefabDrop(const std::string& prefabName, float relX, float relY);
	/// <summary>カメラ相対のオフセットをワールド座標にする</summary>
	/// <param name="off">カメラのローカルのオフセット</param>
	/// <returns>ワールド座標（カメラが無ければ off のまま）</returns>
	Vector3 CameraOffsetToWorld(const Vector3& off) const;
	/// <summary>ワールド座標をカメラ相対のオフセットにする（CameraOffsetToWorld の逆）</summary>
	/// <param name="world">ワールド座標</param>
	/// <returns>カメラのローカルのオフセット（カメラが無ければ world のまま）</returns>
	Vector3 WorldToCameraOffset(const Vector3& world) const;

	/// <summary>谷の障害物が崩れた部品か（シーン JSON に保存しない）</summary>
	/// <param name="entity">調べる実体</param>
	/// <returns>崩れた部品なら true</returns>
	bool IsObstacleDebris(const IImGuiEditable* entity) const;
	/// <summary>敵が落とした物か（シーン JSON に保存しない。22_ItemDrop.md）</summary>
	/// <param name="entity">調べる実体</param>
	/// <returns>落とし物なら true</returns>
	bool IsLootDrop(const IImGuiEditable* entity) const;
	/// <summary>落ちている物を全部拾ったことにする（レールのフェーズを抜けるとき）</summary>
	void CollectAllLoot();

private:
	// IObstacleDebrisHost（IRailStageHost へ取り次ぐ）
	/// <summary>崩れた部品のプレハブを置く（当たり判定のない部品。ホストの SpawnEnemyAt へ取り次ぐ）</summary>
	/// <param name="prefab">部品のプレハブ名</param>
	/// <param name="pos">置く位置（ワールド座標）</param>
	/// <returns>置いた部品。置けなければ nullptr</returns>
	IImGuiEditable* SpawnDebrisPiece(const std::string& prefab, const Vector3& pos) override;
	/// <summary>SpawnDebrisPiece で置いた部品を消す（ホストの DestroyEntity へ取り次ぐ）</summary>
	/// <param name="entity">消す部品</param>
	void DestroyDebrisPiece(IImGuiEditable* entity) override;
	// ILootDropsHost（同じく IRailStageHost へ取り次ぐ）
	/// <summary>落とし物のプレハブを置く（当たり判定のない見た目だけ。ホストの SpawnEnemyAt へ取り次ぐ）</summary>
	/// <param name="prefab">出すプレハブ名</param>
	/// <param name="pos">出す位置（ワールド）</param>
	/// <returns>出した実体。出せなければ nullptr</returns>
	IImGuiEditable* SpawnLootPiece(const std::string& prefab, const Vector3& pos) override;
	/// <summary>落とし物の実体を消す（ホストの DestroyEntity へ取り次ぐ）</summary>
	/// <param name="entity">消す実体</param>
	void DestroyLootPiece(IImGuiEditable* entity) override;
	/// <summary>自機の足元（落とし物を吸い寄せる先）</summary>
	/// <param name="out">足元の位置の書き込み先</param>
	/// <returns>自機がいなければ false</returns>
	bool GetLootCollectPoint(Vector3& out) const override;
	/// <summary>シーンをまたいで残るプレイの状態を取得する（ホストの GetGameSession へ取り次ぐ）</summary>
	/// <returns>プレイの状態</returns>
	GameSession* GetGameSession() const override;

	IRailStageHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	// 走行スプライン。所有はシーン（dynamicSplines_）側で、ここは参照のみ。
	// シーンのロードで作り直されるので RebindCameraPath() で貼り直す。
	SplineCurveActor* cameraPath_ = nullptr;
	// シーンに CameraPathSpline がまだ無いときの種。従来の直書き値と同じなので、
	// シーン JSON が無くても以前と同じ挙動になる。
	std::vector<Vector3> defaultCameraPoints_;
	std::vector<std::unique_ptr<CameraRotKey>> cameraRotKeys_;
	std::unique_ptr<RailCameraController> railCamera_;
	std::unique_ptr<RailAimController> railAim_;
	bool aimAuthoring_ = false, prevAimAuthoring_ = false;
	float prevTimeScales_[static_cast<int>(TimeGroup::Count)] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float railCameraSpeed_ = 1.0f / 120.0f;

	// 大河の上でカメラと自機を水面より下へ入れない（11_LowAltitudeFlight.md L4）。
	// 窓は秒で持ち、毎フレーム進行度へ換算してカメラへ渡す（speed を変えても窓がずれない）。
	struct WaterClamp {
		bool  enabled       = true;
		float startSec      = 60.0f;   // 雲抜けの降下中〜
		float endSec        = 119.0f;  // 〜縁を越える直前まで（120s はもう縁の先で Y=0。そこで外すと跳ねる）
		float surfaceY      = 0.0f;    // 水面の高さ（§3.0）
		float cameraMinY    = 2.0f;    // カメラの下限（水面からではなくワールド Y）。低空 3.5m より十分下に置く
		float cameraSoftness = 0.5f;   // 押し上げのなめらかさ[m]
		float playerMargin  = 0.3f;    // 自機のコライダー下端と水面の余白[m]
	} waterClamp_;

	// 低空の間だけ接線の向きに足す見下ろし角（rotKeys が無い時だけ効く。L8 が rotKeys を書けばそちらが優先）。
	// 視野が狭い（FovY 0.45）ので 10° を超えると地平線が画面の上へ出る。
	struct LowPitch {
		bool  enabled  = true;
		float deg      = 5.0f;
		float startSec = 72.0f;    // 雲抜けの降下が水平に近づく頃〜
		float endSec   = 119.0f;   // 〜縁を越える直前まで
		float fadeSec  = 3.0f;     // 窓の内側でこの秒数かけて入る／抜ける
	} lowPitch_;
	/// <summary>水面クランプと低空の見下ろし角の窓を、秒から進行度へ換算してレールカメラへ渡す</summary>
	void ApplyWaterClampToCamera();

	WaveDef currentWave_;
	// 谷の障害物（enemy_type "Obstacle"）が壊れた後の崩れ方（4_StageGimmick.md §11）
	ObstacleDebris obstacleDebris_;
	// 敵が落とすアイテムとエーテル（22_ItemDrop.md）
	LootDrops lootDrops_;
	std::vector<bool> spawnFired_, retreatFired_;
	std::vector<float> killAtT_;
	std::string wavePath_ = "Resources/Json/Waves/stage1.json";

#ifdef _DEBUG
	bool waveEditMode_ = false; int waveSelectedEntry_ = -1; float waveDropDepth_ = 30.0f;
	// Wave JSON のホットリロード監視（Blender の Export をその場で反映する）
	std::filesystem::file_time_type waveLastWriteTime_{};
	bool waveWatchInitialized_ = false;
	bool autoReloadWave_ = true;
	/// <summary>今の Wave を JSON へ保存し、自分の保存で読み直さないよう更新時刻を記録する</summary>
	void SaveWaveToDisk();
	/// <summary>出現・退避・撃破の記録をエントリ数に合わせて作り直し、今の時刻へ Seek して敵を出し直す</summary>
	void RebuildWaveRuntimeState();
	/// <summary>Wave Editor の UI（編集モード・エントリの一覧と編集・保存・読み直し）</summary>
	/// <param name="changed">値を書き換えたら true にする</param>
	void DrawWaveEditorUI(bool& changed);
#endif
};

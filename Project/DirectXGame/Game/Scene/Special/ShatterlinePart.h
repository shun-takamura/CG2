#pragma once
#include "Effect/EffectManager.h"  // EffectHandle
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Camera;
class DirectXCore;
class EffectPrimitiveRenderer;
class IImGuiEditable;
class ISpecialMoveHost;
class JsonValue;
class ScreenShardRenderer;
class SRVManager;
struct ID3D12GraphicsCommandList;

/// <summary>
/// 必殺技 Shatterline（線・瞬間・最高単発火力型。設計書 disruptor_spec）。
/// 画面中央を支点に角度だけで決まる線を引き、線上の敵を即死（ボスは固定ダメージ）させて、
/// 反転した世界がガラスのように割れて戻る。段取りは Charge → Slash → Collapse → Recover。
/// 実時間（UI の dt）で進む。World はこの技が止める／戻す。
/// </summary>
class ShatterlinePart {
public:
	ShatterlinePart();
	~ShatterlinePart();

	/// <summary>シーンへの窓口とカメラを受け取る</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">演出で動かすカメラ</param>
	void Initialize(ISpecialMoveHost* host, Camera* camera);
	/// <summary>破片レンダラの GPU リソースを作る（シーンの後回しの初期化から呼ぶ）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void InitializeRenderer(DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>再生中のエフェクトを止め、描画物と破片レンダラを解放する</summary>
	void Finalize();

	/// <summary>技を発動して Charge へ入る</summary>
	void Begin();
	/// <summary>発動中の毎フレームの更新。フェーズを進め、照準・ビーム・崩壊を動かす</summary>
	/// <param name="firePressed">射撃ボタンを押したフレームなら true（線の角度を確定する）</param>
	/// <param name="realDt">実時間の経過 [秒]</param>
	/// <returns>技が終わったら true</returns>
	bool Update(bool firePressed, float realDt);
	/// <summary>演出中のカメラを書く（シーンの通常カメラが書いた後に上書きする）</summary>
	/// <param name="realDt">実時間の経過 [秒]</param>
	void ApplyCamera(float realDt);
	/// <summary>終了・中断（シーンの EndSpecialMove から）。色反転や再生中のエフェクトも止める</summary>
	void Reset();
	/// <summary>遅延削除の待ち行列を進める（技が終わった後も毎フレーム呼ぶ）</summary>
	void UpdateTrash();

	/// <summary>シーンの描画パスの中で、崩壊の元絵のキャプチャと断裂線を描く</summary>
	/// <param name="commandList">描画先のコマンドリスト</param>
	void DrawScene(ID3D12GraphicsCommandList* commandList);
	/// <summary>ポストエフェクトの後に反転したガラスの破片を描く（シーンの中で描くと色反転が二重にかかる）</summary>
	void DrawShards();
	/// <summary>割り方（Voronoi セル）の境界線のプレビューを描く（Debug）</summary>
	void DrawDebug();

	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	/// <param name="changed">値を変えたら true にする（シーンが JSON へ保存する）</param>
	void OnImGui(bool& changed);

private:
	enum class Phase {
		Idle,
		Charge,    // チャージ(収束)：World スロー＋無敵＋プレイヤー中央＋カメラ引き
		Slash,     // 一閃：World 停止
		Collapse,  // 崩壊：World 停止
		Recover,   // 復帰＋後隙：World 再開・無敵なし・入力不可
	};

	// Voronoi のセル（スクリーン UV の凸多角形）
	struct Cell {
		std::vector<Vector2> polyUV;          // セル凸多角形の頂点（スクリーンUV [0,1]）
		Vector2 centroidUV{ 0.5f, 0.5f };     // 重心（スクリーンUV）
	};
	// 割れたセルの飛び方
	struct CellRuntime {
		bool    broken    = false;       // 既に割れて飛んだか
		float   age       = 0.0f;        // broken からの経過秒
		float   lifeDur   = 0.7f;        // 寿命（秒）
		float   breakDist = 0.0f;        // 切断線からの垂直距離（割れ順の閾値、build 時に確定）
		Vector3 vel{ 0.0f, 0.0f, 0.0f }; // 飛散速度（world）
		Vector3 spin{ 0.0f, 0.0f, 0.0f };// 回転速度（rad/s）
		Vector3 rot{ 0.0f, 0.0f, 0.0f }; // 現在回転
		Vector3 offset{ 0.0f, 0.0f, 0.0f }; // 重心からの累積変位（world）
	};
	// GPU が前フレームのコマンドを実行中に解放しないよう、数フレーム持ってから捨てる
	struct TrashEntry {
		std::unique_ptr<EffectPrimitiveRenderer> renderer;
		int framesLeft = 4;
	};

	/// <summary>フェーズを切り替え、World の時間と無敵をそのフェーズ用に切り替える</summary>
	/// <param name="p">次のフェーズ</param>
	void  EnterPhase(Phase p);
	/// <summary>画面中央からレティクルへの角度を求める</summary>
	/// <returns>角度 [ラジアン]（0 で水平）</returns>
	float AngleFromReticle() const;
	/// <summary>画面端まで伸ばした切断線と中央の支点を LineRenderer(3D) へ積む</summary>
	void  DrawAimLine();
	/// <summary>一閃：線上の敵・敵弾を集め、切断線をワールドへ焼き付ける（キルは遅延）</summary>
	void  ExecuteSlash();
	/// <summary>集めた対象を一括でキルする（断裂線が引き切った瞬間に呼ぶ）</summary>
	void  ExecuteKills();
	/// <summary>Slash に入ったときに発射ビームと衝撃波のエフェクトを出す</summary>
	void  BuildFireVisuals();
	/// <summary>Collapse に入ったときに断裂線のビームを作る</summary>
	void  BuildRift();
	/// <summary>断裂線の走り込み・色反転・セルの割れを進める</summary>
	/// <param name="realDt">実時間の経過 [秒]</param>
	void  UpdateVisuals(float realDt);
	/// <summary>描画物を遅延削除の待ち行列へ移す</summary>
	/// <param name="r">移す描画物（移した後は空になる。空なら何もしない）</param>
	void  TrashVisual(std::unique_ptr<EffectPrimitiveRenderer>& r);
	/// <summary>反転シェーダの ON/OFF を切り替える（Slash の全反転と Collapse のつなぎの全反転だけ ON）</summary>
	void  UpdateReveal();
	/// <summary>Collapse の割れの進み具合を求める</summary>
	/// <returns>進み具合 0〜1（smoothstep 済み）</returns>
	float CollapseRevealT() const;
	/// <summary>seed から Voronoi のセルを作り cells_ に入れる</summary>
	void  BuildCells();
	/// <summary>cells_ をワールドの三角形にして破片レンダラへ渡す（崩壊中に 1 回）</summary>
	void  BuildCellMeshesAndUpload();
	/// <summary>割れの境界の進みに合わせてセルを順に割り、飛散・回転・フェードを進める</summary>
	/// <param name="realDt">実時間の経過 [秒]</param>
	void  UpdateCells(float realDt);
	/// <summary>セルの境界線を割れる順で色分けして LineRenderer へ積む（スクラブ対応）</summary>
	void  DrawCellBordersDebug();

	ISpecialMoveHost* host_ = nullptr;
	Camera* camera_ = nullptr;

	Phase phase_      = Phase::Idle;
	float phaseTimer_ = 0.0f;  // 現フェーズ経過秒（実時間）
	// フェーズ長（秒、実時間）
	float chargeDuration_   = 2.5f;  // チャージ（狙う猶予）
	float slashDuration_    = 1.5f;  // 一閃＝発射ショットの表示尺（カメラ到達は門番で別途保証）
	float collapseDuration_ = 2.5f;  // 崩壊（リビールの剥がれ時間も兼ねる）
	float recoverDuration_  = 2.0f;  // 後隙（無敵なし・入力不可）
	float chargeTimeScale_  = 0.0f;  // チャージ中の World 時間倍率（0=停止＝敵が固定で狙いやすい。>0でスロー）
	// Slash 中の発射タイミング：カメラが発射ショット位置へ回り込み切ってからビーム発射し、
	// 発射から少し遅れて色反転を入れる（回り込み→発射→一瞬遅れて反転→崩壊 の順を保証）。
	bool  fired_       = false;  // この一閃でビーム発射済みか（カメラ到達後に true）
	float fireElapsed_ = 0.0f;   // 発射からの経過秒（色反転ディレイ／表示尺に使用）
	float invertDelay_ = 0.1f;   // 発射から色反転 ON までの遅れ（秒）
	// Collapse：カメラが「狙うアングル」へ戻り切ってから崩壊を進めるタイマー
	float collapseRevealTimer_ = 0.0f;

	// 照準（中央支点・1点置き・角度自由）。スクリーン空間、深度無視。0=横一閃(水平)
	float aimAngle_     = 0.0f;   // 切断線の角度(rad)。チャージ中はレティクルへライブ追従
	bool  aimConfirmed_ = false;  // 射撃ボタンで角度を確定したか
	float lineWidthPx_  = 40.0f;  // スクリーン空間の線への距離しきい値(px, 半幅)
	int   bossDamage_   = 1000;   // ボスへの固定ダメージ
	Vector4 aimPreviewColor_{ 1.0f, 0.85f, 0.2f, 0.6f }; // 予測線（調整中）＝半透明の金
	Vector4 aimConfirmColor_{ 0.6f, 0.4f, 1.0f, 1.0f };  // 確定線＝紫
	float   pivotRadiusPx_  = 14.0f;                     // 中央支点マーカー（リング）の半径(px)
	Vector4 pivotColor_{ 1.0f, 1.0f, 1.0f, 0.9f };       // 支点マーカー色（白）

	// カメラ演出。Charge=後方引き(狙う) → Slash=左横やや前・あおり(発射ショット) → Collapse=後方復帰 → Recover=通常へ。
	// 切断線は一閃でワールド空間へ焼き付くので、アングルを変えても線と敵がズレない。
	float camPullback_ = 8.0f;   // forward 逆方向への後退量（Charge 時）
	float camUpAdd_    = 1.5f;   // up 方向の持ち上げ（Charge 時）
	float camFovAdd_   = 0.06f;  // FovY 加算（Charge 時）
	float camEaseTime_ = 0.25f;  // 1フェーズ分のカメラ移動時間（秒・決定的smoothstep。これが終わるまで次フェーズへ進まない）
	bool    camActive_ = false;   // 演出中フラグ
	bool    camInit_   = false;   // 補間状態を実カメラで初期化済みか
	Vector3 camEyeCur_{ 0.0f, 0.0f, 0.0f };  // 補間中のカメラ位置
	Vector3 camLookCur_{ 0.0f, 0.0f, 0.0f }; // 補間中の注視点
	float   camFovCur_ = 0.0f;    // 補間中の FovY
	// 決定的なフェーズ間移動（前の移動が完了してから次フェーズへ＝手続き形式）
	Phase   camMovePhase_ = Phase::Idle; // 現在移動中の対象フェーズ（変化検出用）
	Vector3 camFromEye_{ 0.0f, 0.0f, 0.0f };  // 移動開始時のカメラ位置
	Vector3 camFromLook_{ 0.0f, 0.0f, 0.0f }; // 移動開始時の注視点
	float   camFromFov_   = 0.0f; // 移動開始時の FovY
	float   camMoveTimer_ = 0.0f; // 現フェーズ移動の経過秒
	bool    camArrived_   = false;// 現フェーズの目標姿勢へ到達したか（フェーズ遷移の門番）
	// 発射ショット（Slash）：プレイヤー基準オフセット（左横やや前・あおり）
	float   camFireSide_   = 6.0f; // プレイヤー左方向の距離
	float   camFireFront_  = 5.0f; // 前方（画面奥）方向の距離
	float   camFireDown_   = 3.0f; // 下げ量（あおり）
	float   camFireLookUp_ = 0.5f; // 注視点の持ち上げ
	float   camFireFovAdd_ = 0.0f; // 発射ショットの FovY 加算
	// Collapse：後方引き（Charge とは別値で管理）
	float   camCollapsePullback_ = 8.0f;
	float   camCollapseUpAdd_    = 1.5f;

	// 一閃の瞬間に確定するワールド空間の切断線（カメラを引いても敵と一緒に世界に残る＝描いた通り）
	bool    cutWorldValid_ = false;
	Vector3 cutWorldP1_{ 0.0f, 0.0f, 0.0f };
	Vector3 cutWorldP2_{ 0.0f, 0.0f, 0.0f };
	float   cutDepth_ = 40.0f;   // ヒット敵が無いときの焼き付け奥行き（敵があれば平均で上書き）

	// 発射ビーム（衝撃波の Ring も含む）とチャージ演出は EffectDef。中身はエフェクトエディターで作る
	std::string  beamEffectName_   = "ShatterlineBeam";    // Resources/Json/Effects/ShatterlineBeam.json
	EffectHandle beamHandle_       = kInvalidEffectHandle;
	std::string  chargeEffectName_ = "ShatterlineCharge";  // Resources/Json/Effects/ShatterlineCharge.json
	EffectHandle chargeHandle_     = kInvalidEffectHandle;
	Vector3 beamDir_{ 0.0f, 0.15f, 1.0f };  // 固定発射方向（ワールド。既定=前方やや上＝戦場へ）。狙い角度とは無関係
	Vector3 beamFireDir_{ 0.0f, 0.0f, 1.0f };// ランタイム：確定発射方向
	Vector3 beamMuzzle_{ 0.0f, 0.0f, 0.0f }; // ランタイム：筒先位置
	// 断裂線ビジュアル（Collapse 中に P1[右]→P2[左] へ走り込む）。太さは判定帯に一致させる
	std::unique_ptr<EffectPrimitiveRenderer> rift_;
	float   riftWidthScale_     = 1.0f;   // 判定帯に対する太さ倍率
	float   riftBakedHalfWidth_ = 0.0f;   // ランタイム：焼き付け時に確定した world 半幅
	Vector4 riftColor_{ 0.6f, 0.4f, 1.0f, 1.0f };
	float   riftRevealTime_ = 0.45f;      // 右→左へ走り込む時間（Collapse 内・決定的）
	float   riftTimer_      = 0.0f;
	std::vector<TrashEntry> trash_;
	// 遅延キル：判定は Slash で収集し、断裂線が引き切った瞬間に一括キル（線＝判定の対応を見せる）
	std::vector<IImGuiEditable*> pendingEnemies_;  // 収集した敵本体（Enemy/Boss）
	std::vector<IImGuiEditable*> pendingBullets_;  // 収集した敵弾
	bool killsDone_ = false;                       // この一閃のキルを実行済みか

	// 崩壊リビール＋色反転（PostEffect の ColorInvert を駆動）
	float revealIntensity_  = 1.0f;   // 反転の強さ（1=完全反転）
	float revealStartDelay_ = 0.45f;  // Collapse 入りからこの秒数は全画面反転（断裂線が走り切るまで殻のまま）
	float collapseEndAt_    = 0.9f;   // 画面がこの割合まで通常色に戻ったら崩壊終了＝後隙(World再開)へ

	// 破片レンダラ（反転世界の殻のかけら）。未割れセル＝静止した反転シャードで“殻”、割れたセルは飛散
	std::unique_ptr<ScreenShardRenderer> shards_;
	bool  captureDone_ = false;       // この崩壊でシーンキャプチャ済みか
	float fragSpin_     = 6.0f;       // 破片の回転速度の上限(rad/s)
	float fragMinScale_ = 0.06f;      // 破片サイズ(1=発生時)がこの比まで縮んだら完全に消す
	float fragAlpha_    = 1.0f;       // 殻・破片の不透明度（1=反転スクリーンをそのまま貼る）
	float fragSatBoost_ = 1.6f;       // 反転色の彩度ブースト（1=そのまま, >1で色を強調）
	static constexpr uint32_t kShardCap_ = 2048; // 破片レンダラの per-cell 描画上限（Cell Count 上限以上に取る）

	// 事前分割セル（手続き Voronoi）。種点はスクリーン空間固定＝同じ seed で同じ割れ方が再現される。
	// 割れ順は「重心の切断線からの垂直距離」で決まり、リビール境界が来た片から順に割れる。
	std::vector<Cell> cells_;
	uint32_t fractureSeed_     = 1u;     // 割り方のシード（同値＝同じ割れ方）
	bool     fractureSeedLock_ = false;  // true: 発動ごとにこの seed を使う / false: 毎回ランダム（使った値は seed に残す）
	int      cellCount_        = 100;    // セル数（種点数）
	bool     cellDebugDraw_      = false; // セル境界線を LineRenderer で表示
	float    cellPreviewRevealT_ = 0.0f;  // 手動スクラブ：この revealT まで割れたとみなして色分け
	std::vector<Vector3> cellCentroidWorld_; // 各セル重心のワールド配置（rest）。飛散の基準にも使う
	bool cellMeshUploaded_ = false;          // この崩壊で SetCells 済みか
	bool cellStaticDraw_   = false;          // 割らずに全セルを静止描画（マッピング確認用）
	std::vector<CellRuntime> cellRuntime_;
	float cellMaxBreakDist_ = 1.0f; // 割れ閾値の正規化用（全画面を覆う最大垂直距離・build 時に確定）
	// 飛散のチューニング
	float breakFreeze_   = 0.12f; // 割れた直後この秒数は静止（バリン）
	float breakPopSpeed_ = 6.0f;  // 視点方向へ飛び出す速さ
	float breakSpread_   = 4.0f;  // ランダム拡散の速さ
	float breakGravity_  = 6.0f;  // 断裂線へ引き寄せる加速
	float breakLifeMin_  = 0.5f;  // ※ ImGui の DragFloat2 で Min/Max を並べて触るので、この 2 つは続けて置く
	float breakLifeMax_  = 1.0f;
};

#pragma once
#include "Effect/EffectManager.h"  // EffectHandle
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Camera;
class EffectPrimitiveRenderer;
class IImGuiEditable;
class ISpecialMoveHost;
class JsonValue;
class LightningRuntime;

/// <summary>
/// 必殺技 Storm Lock（範囲・多段・殲滅型）。
/// Barrier（自機を囲むバリアで敵弾を消す）→ Lockon（World を止め、画面内の敵・敵弾を近い順にロックオンしてチャージ）
/// → Fire（World を戻し、ロックした対象と新しく入ってきた対象を雷で順に拘束して削る）→ End（余韻の後に終了）。
/// バリアと光の翼は全フェーズで出す。
/// </summary>
class StormLockPart {
public:
	StormLockPart();
	~StormLockPart();

	/// <summary>シーンへの窓口とカメラを受け取る</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">演出で動かすカメラ</param>
	void Initialize(ISpecialMoveHost* host, Camera* camera);
	/// <summary>再生中のエフェクトを止め、描画物を解放する</summary>
	void Finalize();

	/// <summary>技を発動して Barrier へ入る</summary>
	void Begin();
	/// <summary>発動中の毎フレームの更新。今のフェーズを進める</summary>
	/// <param name="worldDt">World の経過 [秒]（Barrier / Fire / End はこれで進む）</param>
	/// <param name="realDt">実時間の経過 [秒]（Lockon はこれで進む）</param>
	/// <returns>技が終わったら true</returns>
	bool Update(float worldDt, float realDt);
	/// <summary>演出中のカメラを今のカメラから後ろ＋上へ引く（シーンの通常カメラが書いた後に呼ぶ）</summary>
	void ApplyCamera();
	/// <summary>終了・中断（シーンの EndSpecialMove から）。描画物は遅延削除へ回す</summary>
	void Reset();
	/// <summary>遅延削除の待ち行列を進め、バリアの球の CB を書く（技が終わった後も毎フレーム呼ぶ）</summary>
	/// <param name="realDt">実時間の経過 [秒]</param>
	void UpdateTrash(float realDt);

	/// <summary>バリアの球・ロックオンの線/リング/キューブ・チャージ電撃・発射の雷を描く</summary>
	void Draw();

	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;

private:
	enum class Phase {
		Idle,
		Barrier,   // バリア展開（通常時間進行、バリア当たり判定）
		Lockon,    // ワールド時間停止 + 順次ロックオン + チャージ
		Fire,      // 時間停止解除 + サンダー発射
		End,       // 終了猶予（バリア停止までの間）
	};

	// GPU が前フレームのコマンドを実行中に解放しないよう、数フレーム持ってから捨てる
	struct TrashEntry {
		std::unique_ptr<EffectPrimitiveRenderer> renderer;
		std::unique_ptr<LightningRuntime> lightning;
		int framesLeft = 4;
	};
	// ロックオン中の 1 対象
	struct LockonTarget {
		IImGuiEditable* entity = nullptr;        // 敵本体 or 敵弾
		bool  isBullet = false;                  // 敵弾か（false=敵本体）
		int   patternIndex = 0;                  // 0=A(真上/左下/右下), 1=B(真下/右上/左上)
		float startTime = 0.0f;                  // Lockon 経過秒（このターゲットのロック開始時刻）
		float radius = 0.5f;                     // 敵 collider 半径
		bool  visualsCreated = false;            // 線・リング・キューブ生成済みか
		std::unique_ptr<EffectPrimitiveRenderer> lines[3];
		std::unique_ptr<EffectPrimitiveRenderer> ring;
		std::unique_ptr<EffectPrimitiveRenderer> cube;
	};
	// チャージ電撃（複数本がプレイヤー周囲の楕円表面で短い弧を描く）
	struct ChargeBolt {
		std::unique_ptr<LightningRuntime> rt;
		float nextRegenTime = 0.0f;  // 次に角度を再ランダム化する時刻（Lockon 経過秒）
		float angleBase = 0.0f;      // 現在の基準角度（ラジアン）
	};
	// 発射の雷（対象を拘束してダメージティックで削る）
	struct FireBolt {
		std::unique_ptr<LightningRuntime> rt;
		IImGuiEditable* entity = nullptr;      // 対象（敵本体 or 敵弾）
		bool   isBullet = false;               // HP を持たない対象（敵弾）か
		float  elapsed = 0.0f;                 // この対象に当て始めてからの経過秒
		float  nextTickTime = 0.0f;            // 次のダメージティック時刻（elapsed 基準）
		Vector3 lastTargetPos{ 0.0f, 0.0f, 0.0f }; // 最後に確認した対象位置（終点用）
		bool   done = false;                   // 終了フラグ（trash へ回収予定）
	};
	struct FireQueueItem { IImGuiEditable* entity = nullptr; bool isBullet = false; };

	/// <summary>Barrier へ入り、バリアの描画物・エフェクト・パーティクル群を用意する</summary>
	void EnterBarrierPhase();
	/// <summary>Barrier の長さが過ぎたら Lockon へ移る</summary>
	/// <param name="worldDt">World の経過 [秒]</param>
	void UpdateBarrierPhase(float worldDt);
	/// <summary>バリアの球を自機に追従させ、球の中の敵弾を消す（全フェーズ共通）</summary>
	void UpdateBarrier();
	/// <summary>回転するワイヤーフレームの球（経線・緯線）を LineRenderer へ積む</summary>
	/// <param name="center">球の中心（ワールド）</param>
	void DrawBarrierWireframe(const Vector3& center);
	/// <summary>光の翼：X 字の方向へ小さいパーティクルを外向きに噴き出す</summary>
	void UpdateWings();
	/// <summary>Lockon へ入り、World を止めてロックオンの対象を並べる</summary>
	void EnterLockonPhase();
	/// <summary>対象を順にロックオンし、全部終わったらチャージ電撃を出してチャージ後に Fire へ移る</summary>
	/// <param name="realDt">実時間の経過 [秒]</param>
	void UpdateLockonPhase(float realDt);
	/// <summary>画面内の敵と敵弾を自機に近い順に並べてロックオンの対象にする</summary>
	void EnumerateLockonTargets();
	/// <summary>対象のロックオン演出（3 本の線・リング・キューブ）を作る（作成済みなら何もしない）</summary>
	/// <param name="t">対象</param>
	void SpawnLockonVisuals(LockonTarget& t);
	/// <summary>対象のロックオン演出を経過時間に合わせて動かす</summary>
	/// <param name="t">対象</param>
	/// <param name="localTime">その対象のロック開始からの経過 [秒]</param>
	void UpdateLockonTargetVisuals(LockonTarget& t, float localTime);
	/// <summary>Fire へ入り、World を再開してロック済みの対象から発射の待ち行列を作る</summary>
	void EnterFirePhase();
	/// <summary>新しく画面に入った対象を待ち行列へ足し、雷を順に撃って拘束・ダメージを進める。全部終わったら End へ移る</summary>
	/// <param name="dt">World の経過 [秒]</param>
	void UpdateFirePhase(float dt);
	/// <summary>End へ入り、フェーズの経過時間を 0 に戻す</summary>
	void EnterEndPhase();
	/// <summary>終了の猶予が過ぎたら finished_ を立てる</summary>
	/// <param name="dt">World の経過 [秒]（使わない。phaseTimer_ は Update 側で進む）</param>
	void UpdateEndPhase(float dt);
	/// <summary>画面に映っている敵弾・敵本体を集める（ロックオンの列挙と Fire 中の新規侵入の検出で共通に使う）</summary>
	/// <param name="out">対象と、それが敵弾かどうか（second が true で敵弾）を追加する先</param>
	void CollectScreenTargets(std::vector<std::pair<IImGuiEditable*, bool>>& out);
	/// <summary>描画物を遅延削除の待ち行列へ移す</summary>
	/// <param name="r">移す描画物（移した後は空になる。空なら何もしない）</param>
	void TrashRenderer(std::unique_ptr<EffectPrimitiveRenderer>& r);
	/// <summary>雷を遅延削除の待ち行列へ移す</summary>
	/// <param name="rt">移す雷（移した後は空になる。空なら何もしない）</param>
	void TrashLightning(std::unique_ptr<LightningRuntime>& rt);
	/// <summary>ロックオンの線・リング・キューブをすべて遅延削除へ回す</summary>
	void TrashLockonVisuals();

	ISpecialMoveHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	float realDt_ = 0.0f;      // このフレームの実時間（演出の回転・CB 更新用）
	bool  finished_ = false;   // End の猶予が終わった

	Phase phase_ = Phase::Idle;
	float phaseTimer_ = 0.0f;  // 現フェーズ内の経過秒

	// ----- Barrier -----
	float   barrierRadius_   = 2.0f;   // バリア球半径
	float   barrierDuration_ = 3.0f;   // Barrier の長さ（秒）
	Vector4 barrierColor_{ 1.0f, 0.8f, 0.3f, 0.10f }; // 薄い金（内膜）
	bool    barrierFillOn_ = false;    // 塗りつぶし半透明球を表示するか（既定 OFF、ワイヤ球のみ）
	std::unique_ptr<EffectPrimitiveRenderer> barrierVis_;
	// バリア本体エフェクト（エフェクトエディタ製）。名前が空でなく登録済みなら、これを再生して追従。
	// 再生中はワイヤーフレーム球は描画しない（空 or 未登録ならワイヤー球にフォールバック）。
	std::string  barrierEffectName_   = "Barrier";
	EffectHandle barrierEffectHandle_ = kInvalidEffectHandle;
	// 回転ワイヤーフレーム球（LineRenderer で経線・緯線を描画）。当たり判定の正確な範囲を可視化
	bool    barrierWireframeOn_   = true;
	Vector3 barrierWireSpin_{ 0.0f, 0.0f, 0.0f };      // 各軸の累積回転角（rad、内部状態）
	Vector3 barrierWireSpinSpeed_{ 0.5f, 1.2f, 0.3f }; // 各軸の回転速度（rad/s、実時間）
	int     barrierWireMeridians_ = 8;      // 経線本数
	int     barrierWireParallels_ = 5;      // 緯線本数
	int     barrierWireSegments_  = 24;     // 1円の分割数
	Vector4 barrierWireColorGold_{ 1.0f, 0.84f, 0.15f, 0.9f }; // 経線=金
	Vector4 barrierWireColorPink_{ 1.0f, 0.35f, 0.80f, 0.9f }; // 緯線=ピンク
	// バリアのパーティクル（GPU パーティクルを球状に連続バースト）。球状ランダムは虫っぽくなるため既定 OFF
	bool    barrierParticleOn_   = false;
	float   barrierEmitInterval_ = 0.02f;   // バースト間隔（秒、実時間）
	int     barrierEmitCount_    = 10;      // 1バーストの粒子数
	float   barrierParticleLife_ = 0.5f;    // 粒子寿命（秒）
	float   barrierParticleRadiusScale_ = 1.0f; // 発生半径 = barrierRadius × これ
	Vector2 barrierParticleScaleMin_{ 0.06f, 0.06f };
	Vector2 barrierParticleScaleMax_{ 0.16f, 0.16f };
	Vector4 barrierParticleColor0_{ 0.5f, 0.85f, 1.0f, 1.0f }; // 発生時の色
	Vector4 barrierParticleColor1_{ 0.2f, 0.5f,  1.0f, 0.0f }; // 寿命末の色（フェード）
	float   barrierEmitAccum_  = 0.0f;      // 内部バーストタイマ
	bool    barrierGroupReady_ = false;     // GPU パーティクルグループ生成済み

	// ----- カメラの引き -----
	float camPullback_ = 8.0f;   // forward 逆方向（ジャスト回避と同じ既定）
	float camFovAdd_   = 0.06f;  // FovY 加算
	float camUpAdd_    = 1.5f;   // up 方向へ持ち上げ（プレイヤーが画面下寄りに）

	std::vector<TrashEntry> trash_;

	// ----- Lockon（ロックオン + チャージ）-----
	std::vector<LockonTarget> lockonTargets_;
	float lockonInterval_        = 0.075f;  // ロックオン開始間隔
	float lockonDuration_        = 0.5f;    // 1体あたりロックオン完了までの秒
	float lockonLineTravelTime_  = 0.2f;    // 線が敵中心へ集まる時間
	float lockonLineHoldTime_    = 0.1f;    // 集まった後の停止時間（線消滅まで）
	float lockonRingAppearTime_  = 0.2f;    // リング出現時刻（ロック開始から）
	float lockonRingFadeoutTime_ = 0.1f;    // ロック完了後リングが消えるまでの時間
	float chargeDuration_        = 1.5f;    // 全ロック後のチャージ秒
	Vector4 lockonColor_{ 0.3f, 0.7f, 1.0f, 1.0f }; // 線・リングの青
	Vector4 cubeColor_  { 1.0f, 0.5f, 0.0f, 0.5f }; // キューブのオレンジ
	float lockonLineWidth_     = 0.04f;
	float lockonRingThickness_ = 0.05f;
	float lockonTimer_         = 0.0f;  // Lockon 経過秒（ロック演出時刻基準）
	bool  allLockonComplete_   = false;
	float chargeStartTime_     = 0.0f;  // Lockon 内での全ロック完了時刻
	std::vector<ChargeBolt> chargeBolts_;
	int   chargeBoltCount_     = 4;     // 同時本数
	float chargeRegenInterval_ = 0.1f;  // 1本ごとの再生成間隔
	float chargeMinLength_     = 0.6f;  // 終点 - 始点 の最小長

	// ----- Fire（サンダー発射）-----
	// ロック済み + 新規侵入のターゲットを順次サンダーで拘束し、ダメージティックで削る。
	std::vector<FireBolt> fireBolts_;
	std::vector<FireQueueItem> fireQueue_;        // 処理待ちターゲット
	std::unordered_set<IImGuiEditable*> fireProcessed_;  // 既に発射枠へ回した対象（再ロック防止）
	float fireLaunchTimer_ = 0.0f;        // 次の発射までの残り秒
	int   fireSimultaneous_   = 3;        // 同時サンダー本数
	float fireMinHold_        = 0.5f;     // 最低拘束時間（HP=0 でもこの秒数は当てる）
	float fireMaxHold_        = 2.0f;     // 最高拘束時間（生存でも強制終了）
	float fireTickInterval_   = 0.1f;     // ダメージティック間隔
	int   fireTickDamage_     = 5;        // 1ティックダメージ
	float fireLaunchInterval_ = 0.1f;     // 次の発射開始間隔
	Vector4 fireColor_{ 0.5f, 0.8f, 1.0f, 1.0f }; // サンダーの色
	// サンダーの見た目（敵へ飛んでいく感 vs 放電感のバランス調整）
	float fireGrowTime_       = 0.08f;    // 始点→ターゲットへ伸びる時間（0で即全長）
	float fireMaxOffsetRatio_ = 0.10f;    // ジグザグの暴れ幅（小=直線的＝飛んでる感）
	float fireBranchProb_     = 0.30f;    // 枝分かれ確率（放電のパチパチ感）
	float fireStartWidth_     = 0.10f;    // 始点(プレイヤー側)の太さ
	float fireEndWidth_       = 0.05f;    // 終点(敵側)の太さ
	float fireBoltLifetime_   = 0.08f;    // 1メッシュの寿命（小=高速にパチパチ再生成）

	// ----- 光の翼（X字方向に「小さいパーティクル」を外向き噴出。アーム1本=GPUパーティクル群1つ）-----
	bool    wingOn_          = true;
	int     wingArmCount_    = 4;          // アーム本数（X字=4）
	float   wingAngleOffset_ = 0.7853982f; // 配置の基準角(rad)。π/4 で X 字
	float   wingSpeed_       = 6.0f;       // 外向き初速（実質長さ ≒ speed×life）
	float   wingLife_        = 0.4f;       // 粒子寿命（秒）
	int     wingBurstCount_  = 6;          // 1アーム・1フレームの粒子数（量）
	float   wingJitter_      = 1.0f;       // 速度ゆらぎ
	float   wingEmitRadius_  = 0.1f;       // 発生位置の散らばり（中心付近）
	Vector2 wingScaleMin_{ 0.04f, 0.04f };
	Vector2 wingScaleMax_{ 0.10f, 0.10f };
	Vector4 wingColorInner_{ 1.0f, 0.85f, 0.20f, 1.0f }; // 金（発生時）
	Vector4 wingColorOuter_{ 1.0f, 0.40f, 0.85f, 0.0f };  // ピンク（寿命末でフェード）

	// ----- End（終了猶予）-----
	float endDuration_ = 1.0f;            // Fire 終了からバリア消失までの猶予秒
};

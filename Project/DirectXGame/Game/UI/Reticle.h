#pragma once

#include "Vector2.h"
#include "Vector4.h"
#include <memory>
#include <string>
#include <array>

class SpriteManager;
class SpriteInstance;
class ControllerInput;

/// <summary>
/// 画面に追従する照準（レティクル）。
/// 入力ソース：マウス（動かしたらカーソル位置にワープ）/ 右スティック（傾き量で相対移動）。
/// 中心がクライアント領域から出ないようにクランプ。
/// </summary>
class Reticle {
public:
	/// <summary>既定の作成（スプライトは Initialize で作る）</summary>
	Reticle();
	/// <summary>破棄（SpriteInstance の完全型が要るので .cpp で定義）</summary>
	~Reticle();

	/// <summary>内側のレティクルと外側パーツ 4 枚のスプライトを作り、画面の中央に置く</summary>
	/// <param name="spriteManager">スプライトの管理</param>
	/// <param name="texturePath">内側のレティクル（通常）のテクスチャ</param>
	void Initialize(SpriteManager* spriteManager,
		const std::string& texturePath = "Resources/Textures/reticle_center.dds");

	/// <summary>
	/// 入力を読んでレティクル位置を更新する。
	/// mouseHovered = true のとき、mouseClient（ゲーム内クライアント座標 1600x900）にワープ。
	/// false なら右スティック入力だけ受け付ける（ImGui を触っている間は反応させない）。
	/// </summary>
	/// <param name="mouseHovered">マウスがゲーム画面の上にあるか</param>
	/// <param name="mouseClient">マウスの位置（ゲーム内クライアント座標）</param>
	/// <param name="mouseMoved">このフレームでマウスが動いたか</param>
	/// <param name="pad">ゲームパッド（nullptr なら右スティックは使わない）</param>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void Update(bool mouseHovered, const Vector2& mouseClient, bool mouseMoved,
		ControllerInput* pad, float deltaTime);

	/// <summary>内側のレティクルと、チャージ中なら外側パーツを描く</summary>
	void Draw();

	/// <summary>レティクルの中心の位置を取得する</summary>
	/// <returns>クライアント空間（ゲーム内部解像度ピクセル）座標</returns>
	const Vector2& GetPosition() const { return position_; }
	/// <summary>レティクルの中心の位置を設定する</summary>
	/// <param name="p">クライアント空間（ゲーム内部解像度ピクセル）座標</param>
	void SetPosition(const Vector2& p) { position_ = p; }

	/// <summary>レティクルを UI 画面の中央へ戻す</summary>
	void ResetToCenter();

	/// <summary>右スティックを倒しきった時の速さを設定する</summary>
	/// <param name="pxPerSec">速さ [px/秒]</param>
	void SetStickSpeed(float pxPerSec) { stickSpeed_ = pxPerSec; }
	/// <summary>右スティックを倒しきった時の速さを取得する</summary>
	/// <returns>速さ [px/秒]</returns>
	float GetStickSpeed() const { return stickSpeed_; }
	/// <summary>通常時の表示サイズを設定する</summary>
	/// <param name="pixels">一辺の大きさ [px]</param>
	void SetSize(float pixels) { sizePx_ = pixels; }
	/// <summary>通常時の表示サイズを取得する</summary>
	/// <returns>一辺の大きさ [px]</returns>
	float GetSize() const { return sizePx_; }

	/// <summary>
	/// 敵にレイがヒットしている時は色を切り替える（ロックオン表示）
	/// </summary>
	/// <param name="on">ロックオン中なら true</param>
	void SetLockOn(bool on);
	/// <summary>ロックオン表示中か</summary>
	/// <returns>ロックオン中なら true</returns>
	bool IsLockedOn() const { return lockedOn_; }

	/// <summary>非ロック時の色を設定する</summary>
	/// <param name="c">色（RGBA）</param>
	void SetNormalColor(const Vector4& c) { normalColor_ = c; }
	/// <summary>ロック中の色を設定する</summary>
	/// <param name="c">色（RGBA）</param>
	void SetLockOnColor(const Vector4& c) { lockOnColor_ = c; }

	/// <summary>
	/// 近接攻撃が当たる距離にロック中の敵がいる時、内側レティクルのテクスチャを
	/// 近接用に差し替える（サイズ・色はそのまま）。状態変化時のみ切替。
	/// </summary>
	/// <param name="inRange">近接が当たる距離なら true</param>
	void SetMeleeInRange(bool inRange);
	/// <summary>近接用のテクスチャに差し替えているか</summary>
	/// <returns>近接レンジ表示中なら true</returns>
	bool IsMeleeInRange() const { return meleeInRange_; }
	/// <summary>近接レンジ表示に使うテクスチャを設定する（次に切り替えた時から効く）</summary>
	/// <param name="path">テクスチャのパス</param>
	void SetMeleeTexture(const std::string& path) { meleeTexturePath_ = path; }

	/// <summary>
	/// ロックオン中の目標サイズ（pixel）を毎フレームセットする。
	/// 呼び出し側で「敵の見かけ半径」を計算して渡す前提。
	/// SetLockOn(true) と組み合わせて使う。
	/// </summary>
	/// <param name="pixels">目標の大きさ [px]</param>
	void SetLockOnTargetSize(float pixels) { lockOnTargetSizePx_ = pixels; }
	/// <summary>ロックオン中の目標サイズを取得する</summary>
	/// <returns>目標の大きさ [px]</returns>
	float GetLockOnTargetSize() const { return lockOnTargetSizePx_; }

	/// <summary>ロックオン中のサイズの安全な下限・上限（clamp）を設定する</summary>
	/// <param name="minPx">下限 [px]</param>
	/// <param name="maxPx">上限 [px]</param>
	void SetLockOnSizeClamp(float minPx, float maxPx) { lockOnMinPx_ = minPx; lockOnMaxPx_ = maxPx; }

	/// <summary>外側パーツの中心からのオフセットの最小・最大を設定する</summary>
	/// <param name="minPx">最小 [px]</param>
	/// <param name="maxPx">最大 [px]</param>
	void SetLockOnSizeClampOutside(float minPx, float maxPx) { lockOnMinPxOutside_ = minPx; lockOnMaxPxOutside_ = maxPx; }
	/// <summary>外側パーツの中心からのオフセットの最小を取得する</summary>
	/// <returns>最小 [px]</returns>
	float GetLockOnMinPxOutside() const { return lockOnMinPxOutside_; }
	/// <summary>外側パーツの中心からのオフセットの最大を取得する</summary>
	/// <returns>最大 [px]</returns>
	float GetLockOnMaxPxOutside() const { return lockOnMaxPxOutside_; }
	/// <summary>外側パーツのオフセットの最小を ImGui で直接編集するためのポインタを取得する</summary>
	/// <returns>最小 [px] の変数へのポインタ</returns>
	float* GetLockOnMinPxOutsidePtr() { return &lockOnMinPxOutside_; }
	/// <summary>外側パーツのオフセットの最大を ImGui で直接編集するためのポインタを取得する</summary>
	/// <returns>最大 [px] の変数へのポインタ</returns>
	float* GetLockOnMaxPxOutsidePtr() { return &lockOnMaxPxOutside_; }

	/// <summary>外側パーツのスプライトサイズの最小・最大を設定する</summary>
	/// <param name="minPx">最小 [px]</param>
	/// <param name="maxPx">最大 [px]</param>
	void SetOuterSizeClamp(float minPx, float maxPx) { outerSizeMinPx_ = minPx; outerSizeMaxPx_ = maxPx; }
	/// <summary>外側パーツのスプライトサイズの最小を取得する</summary>
	/// <returns>最小 [px]</returns>
	float GetOuterSizeMin() const { return outerSizeMinPx_; }
	/// <summary>外側パーツのスプライトサイズの最大を取得する</summary>
	/// <returns>最大 [px]</returns>
	float GetOuterSizeMax() const { return outerSizeMaxPx_; }
	/// <summary>外側パーツのスプライトサイズの最小を ImGui で直接編集するためのポインタを取得する</summary>
	/// <returns>最小 [px] の変数へのポインタ</returns>
	float* GetOuterSizeMinPtr() { return &outerSizeMinPx_; }
	/// <summary>外側パーツのスプライトサイズの最大を ImGui で直接編集するためのポインタを取得する</summary>
	/// <returns>最大 [px] の変数へのポインタ</returns>
	float* GetOuterSizeMaxPtr() { return &outerSizeMaxPx_; }

	/// <summary>
	/// チャージアニメーションを始める（外側パーツを出現させて、収束させる）。
	/// </summary>
	/// <param name="startRadius">チャージ完了時の外側パーツ半径（広い位置）[px]</param>
	/// <param name="endRadius">収束後の最終半径（狭い位置）[px]</param>
	/// <param name="duration">アニメーション時間 [秒]（0 以下なら 0.3 秒）</param>
	void StartChargeAnimation(float startRadius, float endRadius, float duration);

	/// <summary>
	/// チャージアニメーション終了（外側パーツを非表示）
	/// </summary>
	void EndChargeAnimation();

	/// <summary>
	/// チャージレベル設定（0.0～1.0。-1.0=チャージなし）。
	/// 外部から毎フレーム呼ぶ必要はない（StartChargeAnimation が管理）。
	/// </summary>
	/// <param name="level">チャージレベル</param>
	void SetChargeLevel(float level) { chargeLevel_ = level; }
	/// <summary>チャージレベルを取得する</summary>
	/// <returns>0.0～1.0。-1.0 = チャージなし</returns>
	float GetChargeLevel() const { return chargeLevel_; }

	/// <summary>外側パーツの回転速度を設定する</summary>
	/// <param name="degreesPerSec">回転速度 [度/秒]</param>
	void SetOuterRotationSpeed(float degreesPerSec) { outerRotationSpeed_ = degreesPerSec; }
	/// <summary>外側パーツの回転速度を取得する</summary>
	/// <returns>回転速度 [度/秒]</returns>
	float GetOuterRotationSpeed() const { return outerRotationSpeed_; }

private:
	std::unique_ptr<SpriteInstance> sprite_;
	std::array<std::unique_ptr<SpriteInstance>, 4> outerParticles_;
	Vector2 position_{ 0.0f, 0.0f };  // クライアント領域内（pixels）
	float stickSpeed_ = 1200.0f;       // 右スティック最大時の 1 秒あたり移動ピクセル
	float sizePx_ = 64.0f;             // 通常時の表示サイズ（正方形）

	// 内側レティクルのテクスチャ差し替え（通常 ↔ 近接）
	std::string normalTexturePath_;                                       // Initialize 時のテクスチャ（通常）
	std::string meleeTexturePath_ = "Resources/Textures/reticle_melee.dds"; // 近接が当たる距離の表示
	bool meleeInRange_ = false;                                           // 近接レンジ表示中フラグ

	// ロックオン演出（色 + サイズ）
	bool lockedOn_ = false;
	Vector4 normalColor_{ 0.3f, 1.0f, 0.4f, 1.0f };   // 非ロック：緑（パルテナ準拠）
	Vector4 lockOnColor_{ 1.0f, 0.25f, 0.25f, 1.0f }; // ロック中：赤
	float lockOnTargetSizePx_ = 96.0f; // 敵の見かけサイズに応じて毎フレ外部から更新される
	float lockOnMinPx_ = 64.0f;        // クランプ下限（敵が遠すぎて小さくなり過ぎないように）
	float lockOnMaxPx_ = 128.0f;       // クランプ上限（敵が近すぎて画面を覆わないように）
	float lockOnMinPxOutside_ = 32.0f;        // 外側パーツのオフセット下限（中心からの距離）
	float lockOnMaxPxOutside_ = 64.0f;        // 外側パーツのオフセット上限（中心からの距離）
	float outerSizeMinPx_ = 32.0f;            // 外側パーツのスプライトサイズ下限
	float outerSizeMaxPx_ = 64.0f;            // 外側パーツのスプライトサイズ上限
	float sizeSmoothTime_  = 0.06f;    // サイズ遷移の Lerp 時定数（秒）
	float currentSizePx_   = 48.0f;    // ランタイム：補間中の実サイズ
	float outerBaseSizePx_ = 64.0f;    // 外側パーツの基準サイズ（拡大率 1.0 のときのサイズ）

	// チャージアニメーション（外側パーツ制御）
	float chargeLevel_ = -1.0f;        // -1.0=チャージなし、0.0～1.0=チャージ状態
	float outerRadius_ = 0.0f;         // 現在の外側パーツ半径
	float outerRadiusTarget_ = 0.0f;   // 目標半径
	float outerRotation_ = 0.0f;       // 外側パーツ共通回転角度（度）
	float outerRotationSpeed_ = 360.0f; // 回転速度（度/秒）
	float outerChargeEasingDuration_ = 0.3f; // チャージアニメ時間（秒）
	float outerChargeEasingElapsed_ = 0.0f;  // 経過時間（秒）
	float outerChargeEasingStart_ = 0.0f;    // 補間開始値
	float outerChargeEasingEnd_ = 0.0f;      // 補間終了値
};

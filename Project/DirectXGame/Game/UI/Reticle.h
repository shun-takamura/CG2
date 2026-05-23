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
	Reticle();
	~Reticle();

	void Initialize(SpriteManager* spriteManager,
		const std::string& texturePath = "Resources/Textures/reticle_center.dds");

	/// <summary>
	/// 入力を読んでレティクル位置を更新する。
	/// mouseHovered = true のとき、mouseClient（ゲーム内クライアント座標 1600x900）にワープ。
	/// false なら右スティック入力だけ受け付ける（ImGui を触っている間は反応させない）。
	/// </summary>
	void Update(bool mouseHovered, const Vector2& mouseClient, bool mouseMoved,
		ControllerInput* pad, float deltaTime);

	void Draw();

	// クライアント空間（ゲーム内部解像度ピクセル）座標
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& p) { position_ = p; }

	// 中央リセット
	void ResetToCenter();

	// チューニング値
	void SetStickSpeed(float pxPerSec) { stickSpeed_ = pxPerSec; }
	float GetStickSpeed() const { return stickSpeed_; }
	void SetSize(float pixels) { sizePx_ = pixels; }
	float GetSize() const { return sizePx_; }

	/// <summary>
	/// 敵にレイがヒットしている時は色を切り替える（ロックオン表示）
	/// </summary>
	void SetLockOn(bool on);
	bool IsLockedOn() const { return lockedOn_; }

	void SetNormalColor(const Vector4& c) { normalColor_ = c; }
	void SetLockOnColor(const Vector4& c) { lockOnColor_ = c; }

	/// <summary>
	/// 近接攻撃が当たる距離にロック中の敵がいる時、内側レティクルのテクスチャを
	/// 近接用に差し替える（サイズ・色はそのまま）。状態変化時のみ切替。
	/// </summary>
	void SetMeleeInRange(bool inRange);
	bool IsMeleeInRange() const { return meleeInRange_; }
	void SetMeleeTexture(const std::string& path) { meleeTexturePath_ = path; }

	/// <summary>
	/// ロックオン中の目標サイズ（pixel）を毎フレームセットする。
	/// 呼び出し側で「敵の見かけ半径」を計算して渡す前提。
	/// SetLockOn(true) と組み合わせて使う。
	/// </summary>
	void SetLockOnTargetSize(float pixels) { lockOnTargetSizePx_ = pixels; }
	float GetLockOnTargetSize() const { return lockOnTargetSizePx_; }

	// サイズの安全上限・下限（clamp）
	void SetLockOnSizeClamp(float minPx, float maxPx) { lockOnMinPx_ = minPx; lockOnMaxPx_ = maxPx; }

	// 外側パーツ用の中心からの最小オフセット・最大オフセット
	void SetLockOnSizeClampOutside(float minPx, float maxPx) { lockOnMinPxOutside_ = minPx; lockOnMaxPxOutside_ = maxPx; }
	float GetLockOnMinPxOutside() const { return lockOnMinPxOutside_; }
	float GetLockOnMaxPxOutside() const { return lockOnMaxPxOutside_; }
	// ImGui で直接編集するためのポインタ（オフセット）
	float* GetLockOnMinPxOutsidePtr() { return &lockOnMinPxOutside_; }
	float* GetLockOnMaxPxOutsidePtr() { return &lockOnMaxPxOutside_; }

	// 外側パーツ用のスプライトサイズの最小・最大
	void SetOuterSizeClamp(float minPx, float maxPx) { outerSizeMinPx_ = minPx; outerSizeMaxPx_ = maxPx; }
	float GetOuterSizeMin() const { return outerSizeMinPx_; }
	float GetOuterSizeMax() const { return outerSizeMaxPx_; }
	// ImGui で直接編集するためのポインタ（サイズ）
	float* GetOuterSizeMinPtr() { return &outerSizeMinPx_; }
	float* GetOuterSizeMaxPtr() { return &outerSizeMaxPx_; }

	/// <summary>
	/// チャージアニメーション開始（外側パーツを出現させて、収束させる）。
	/// startRadius: チャージ完了時の外側パーツ半径（広い位置）
	/// endRadius: 収束後の最終半径（狭い位置）
	/// duration: アニメーション時間（秒）
	/// </summary>
	void StartChargeAnimation(float startRadius, float endRadius, float duration);

	/// <summary>
	/// チャージアニメーション終了（外側パーツを非表示）
	/// </summary>
	void EndChargeAnimation();

	/// <summary>
	/// チャージレベル設定（0.0～1.0。-1.0=チャージなし）。
	/// 外部から毎フレーム呼ぶ必要はない（StartChargeAnimation が管理）。
	/// </summary>
	void SetChargeLevel(float level) { chargeLevel_ = level; }
	float GetChargeLevel() const { return chargeLevel_; }

	// チューニング値
	void SetOuterRotationSpeed(float degreesPerSec) { outerRotationSpeed_ = degreesPerSec; }
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

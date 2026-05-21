#pragma once

#include "Vector2.h"
#include "Vector4.h"
#include <memory>
#include <string>

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
		const std::string& texturePath = "Resources/Textures/circle.dds");

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
	/// ロックオン中の目標サイズ（pixel）を毎フレームセットする。
	/// 呼び出し側で「敵の見かけ半径」を計算して渡す前提。
	/// SetLockOn(true) と組み合わせて使う。
	/// </summary>
	void SetLockOnTargetSize(float pixels) { lockOnTargetSizePx_ = pixels; }
	float GetLockOnTargetSize() const { return lockOnTargetSizePx_; }

	// サイズの安全上限・下限（clamp）
	void SetLockOnSizeClamp(float minPx, float maxPx) { lockOnMinPx_ = minPx; lockOnMaxPx_ = maxPx; }

private:
	std::unique_ptr<SpriteInstance> sprite_;
	Vector2 position_{ 0.0f, 0.0f };  // クライアント領域内（pixels）
	float stickSpeed_ = 1200.0f;       // 右スティック最大時の 1 秒あたり移動ピクセル
	float sizePx_ = 64.0f;             // 通常時の表示サイズ（正方形）

	// ロックオン演出（色 + サイズ）
	bool lockedOn_ = false;
	Vector4 normalColor_{ 0.3f, 1.0f, 0.4f, 1.0f };   // 非ロック：緑（パルテナ準拠）
	Vector4 lockOnColor_{ 1.0f, 0.25f, 0.25f, 1.0f }; // ロック中：赤
	float lockOnTargetSizePx_ = 96.0f; // 敵の見かけサイズに応じて毎フレ外部から更新される
	float lockOnMinPx_ = 64.0f;        // クランプ下限（敵が遠すぎて小さくなり過ぎないように）
	float lockOnMaxPx_ = 256.0f;       // クランプ上限（敵が近すぎて画面を覆わないように）
	float sizeSmoothTime_  = 0.06f;    // サイズ遷移の Lerp 時定数（秒）
	float currentSizePx_   = 48.0f;    // ランタイム：補間中の実サイズ
};

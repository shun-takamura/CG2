#pragma once

#include "Vector2.h"
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

private:
	std::unique_ptr<SpriteInstance> sprite_;
	Vector2 position_{ 0.0f, 0.0f };  // クライアント領域内（pixels）
	float stickSpeed_ = 1200.0f;       // 右スティック最大時の 1 秒あたり移動ピクセル
	float sizePx_ = 48.0f;             // スプライトの表示サイズ（正方形）
};

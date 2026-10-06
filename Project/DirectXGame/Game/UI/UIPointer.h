#pragma once

#include "Vector2.h"

class InputManager;

/// <summary>
/// UI 用のマウスの状態（ゲーム画面のピクセル座標）。各シーンが毎フレーム1回作って UI に渡す。
/// Debug は ImGui のビューポート窓に描いているので、窓の中の位置をゲーム画面の解像度へ換算する。
/// Release はスワップチェーン直書きなので、ウィンドウのクライアント座標をそのまま使う。
/// </summary>
struct UIPointer {
	bool valid = false;       // マウスがゲーム画面の上にある
	Vector2 position{};       // ゲーム画面のピクセル座標（TextRenderer / Sprite の描画座標と同じ）
	bool moved = false;       // このフレームで動いた
	bool clicked = false;     // ゲーム画面の上で左ボタンを押した（Debug の ImGui パネル上のクリックは数えない）

	static UIPointer FromInput(InputManager* input);
};

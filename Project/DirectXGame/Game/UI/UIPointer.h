#pragma once

#include "Vector2.h"

class InputManager;

/// <summary>
/// UI 用のマウスの状態（ゲーム画面のピクセル座標）。各シーンが毎フレーム1回作って UI に渡す。
/// Debug は ImGui のビューポート窓に描いているので、窓の中の位置をゲーム画面の解像度へ換算する。
/// Release はスワップチェーンに 16:9 の枠（外は黒帯）で出すので、クライアント座標から黒帯を引いて換算する（MouseInput::GetUIPosition）。
/// </summary>
struct UIPointer {
	bool valid = false;       // マウスがゲーム画面の上にある
	Vector2 position{};       // ゲーム画面のピクセル座標（TextRenderer / Sprite の描画座標と同じ）
	bool moved = false;       // このフレームで動いた
	bool clicked = false;     // ゲーム画面の上で左ボタンを押した（Debug の ImGui パネル上のクリックは数えない）
	bool pressed = false;     // ゲーム画面の上で左ボタンを押している（長押し。賭ける画面の ◀ ▶ など）

	/// <summary>今のフレームのマウスの状態を作る（Debug はビューポート窓、Release は黒帯を引いて換算）</summary>
	/// <param name="input">入力の管理（nullptr なら valid = false）</param>
	/// <returns>マウスの状態。ゲーム画面の外なら valid = false</returns>
	static UIPointer FromInput(InputManager* input);
};

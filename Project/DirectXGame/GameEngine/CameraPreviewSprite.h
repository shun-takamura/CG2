#pragma once
#include <memory>

class SpriteManager;
class SpriteInstance;

/// <summary>
/// カメラキャプチャ映像をスプライトとして表示するヘルパークラス（QR コードの読み取りは Scene が行う）。
/// CameraCapture::OpenCamera() / CloseCamera() は外部（ImGuiなど）で呼ぶ前提。
/// </summary>
class CameraPreviewSprite
{
public:
	/// <summary>何もしない（不完全型 SpriteInstance を unique_ptr で持つため、.cpp で定義する）</summary>
	CameraPreviewSprite();
	/// <summary>スプライトを破棄する（不完全型 SpriteInstance を unique_ptr で持つため、.cpp で定義する）</summary>
	~CameraPreviewSprite();

	/// <summary>スプライトの描画に使うマネージャを受け取る</summary>
	/// <param name="spriteManager">SpriteManager</param>
	void Initialize(SpriteManager* spriteManager);

	/// <summary>
	/// カメラが開いてテクスチャができたらスプライト（640×480、左上）を作り、閉じたら破棄する。
	/// シーンのUpdate()内で呼ぶ。
	/// </summary>
	void Update();

	/// <summary>
	/// カメラテクスチャをGPUに転送してからスプライトを描画する。
	/// シーンのDraw()内で呼ぶ。
	/// </summary>
	void Draw();

private:
	SpriteManager* spriteManager_ = nullptr;
	std::unique_ptr<SpriteInstance> sprite_;
};

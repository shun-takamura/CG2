#pragma once
#include <memory>

class SpriteManager;
class SpriteInstance;

/// <summary>
/// カメラキャプチャ映像をスプライトとして表示し、QRコード読み取りも行うヘルパークラス。
/// CameraCapture::OpenCamera() / CloseCamera() は外部（ImGuiなど）で呼ぶ前提。
/// </summary>
class CameraPreviewSprite
{
public:
	// 不完全型 SpriteInstance を unique_ptr で持つため、デストラクタは .cpp で定義する
	CameraPreviewSprite();
	~CameraPreviewSprite();

	void Initialize(SpriteManager* spriteManager);

	/// <summary>
	/// カメラ開閉に合わせてスプライトのライフサイクルを管理し、QRコードをデコードする。
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

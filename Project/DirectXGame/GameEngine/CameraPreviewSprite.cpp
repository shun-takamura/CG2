#include "CameraPreviewSprite.h"
#include "CameraCapture.h"
#include "TextureManager.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"

namespace {
	// カメラのプレビューを出すスプライトの幅 [px]
	constexpr float kPreviewWidth = 640.0f;
	// カメラのプレビューを出すスプライトの高さ [px]
	constexpr float kPreviewHeight = 480.0f;
}

CameraPreviewSprite::CameraPreviewSprite() = default;
CameraPreviewSprite::~CameraPreviewSprite() = default;

void CameraPreviewSprite::Initialize(SpriteManager* spriteManager)
{
	spriteManager_ = spriteManager;
}

void CameraPreviewSprite::Update()
{
	auto* cam = CameraCapture::GetInstance();

	// カメラが開かれてテクスチャが準備できたらスプライトを生成
	if (!sprite_ && cam->IsOpened() &&
		TextureManager::GetInstance()->HasTexture(CameraCapture::GetTextureName()))
	{
		sprite_ = std::make_unique<SpriteInstance>();
		sprite_->Initialize(spriteManager_, CameraCapture::GetTextureName(), "CameraPreview");
		sprite_->SetPosition({ 0.0f, 0.0f });
		sprite_->SetSize({ kPreviewWidth, kPreviewHeight });
		sprite_->SetTextureLeftTop({ 0.0f, 0.0f });
		sprite_->SetTextureSize({
			static_cast<float>(cam->GetFrameWidth()),
			static_cast<float>(cam->GetFrameHeight())
		});
	}

	// カメラが閉じられたらスプライトを破棄
	if (sprite_ && !cam->IsOpened()) {
		sprite_.reset();
	}

	if (sprite_) {
		sprite_->Update();
	}
}

void CameraPreviewSprite::Draw()
{
	auto* cam = CameraCapture::GetInstance();
	if (!cam->IsOpened()) return;

	cam->UpdateTexture();

	if (sprite_) {
		spriteManager_->DrawSetting();
		sprite_->Draw();
	}
}

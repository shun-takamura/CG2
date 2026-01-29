#pragma once
#include <memory>
#include "BaseScene.h"

// 前方宣言
class SpriteInstance;

/// <summary>
/// タイトルシーン
/// </summary>
class TitleScene : public BaseScene {
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	TitleScene();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~TitleScene() override;

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 更新
	/// </summary>
	void Update() override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw() override;

private:
	// タイトル用スプライト（uvChecker）
	std::unique_ptr<SpriteInstance> titleSprite_;
	std::unique_ptr<SpriteInstance> testStripe_;
};
#pragma once
#include "BaseScene.h"
#include "SpriteInstance.h"

class GameOver :  public BaseScene
{
public:

	~GameOver() override;

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

	// スプライト
	std::unique_ptr<SpriteInstance> sprite_;

};


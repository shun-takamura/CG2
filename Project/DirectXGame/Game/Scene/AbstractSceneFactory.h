#pragma once
#include<memory>
#include <string>

// 前方宣言
class BaseScene;

/// <summary>
/// シーン工場（概念）
/// 抽象クラス - 継承して使う前提
/// </summary>
class AbstractSceneFactory {
public:
	/// 仮想デストラクタ
	virtual ~AbstractSceneFactory() = default;

	/// <summary>
	/// シーン生成
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <returns>生成したシーン</returns>
	virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
};

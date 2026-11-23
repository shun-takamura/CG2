#pragma once
#include<memory>
#include <string>

// 前方宣言
class Scene;

/// <summary>
/// シーン工場（概念）
/// 抽象クラス - 継承して使う前提。生成物はエンジン基底 Scene として返す。
/// </summary>
class AbstractSceneFactory {
public:
	/// 仮想デストラクタ
	virtual ~AbstractSceneFactory() = default;

	/// <summary>
	/// シーン名から対応するシーンを作る（マネージャの注入と Initialize は呼び出し側が行う）
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <returns>生成したシーン（知らない名前なら nullptr）</returns>
	virtual std::unique_ptr<Scene> CreateScene(const std::string& sceneName) = 0;
};

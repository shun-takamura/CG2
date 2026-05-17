#pragma once

#include <vector>

class IImGuiEditable;

/// <summary>
/// 球コライダーの登録・判定を一元管理する（シングルトン）。
/// IImGuiEditable の構築/破棄時に自動的に Register/Unregister される。
/// 毎フレーム Update() で全ペアを評価し、衝突発生時にコールバックを呼ぶ。
/// Debug ビルドでは showDebug が true のコライダーを DebugDraw::Sphere で描画する。
/// </summary>
class CollisionManager {
public:
	static CollisionManager* GetInstance();

	void Register(IImGuiEditable* e);
	void Unregister(IImGuiEditable* e);

	/// <summary>
	/// 全ペアの衝突判定を実施。SceneManager から毎フレーム呼ばれる。
	/// </summary>
	void Update();

	/// <summary>
	/// Debug 用：コライダーを線描画キューに積む。
	/// Update の中で自動的に呼ばれるが、外から強制描画したい場合のために公開しておく。
	/// </summary>
	void DrawDebug();

	//====================
	// グローバル設定（Debug 用）
	//====================

	/// <summary>
	/// すべてのコライダーのデバッグ描画を一括 ON/OFF。デフォルト ON。
	/// 個別の collider.showDebug が false でもこの全体スイッチが OFF ならどこも描画しない。
	/// </summary>
	bool IsDrawDebugEnabled() const { return drawDebugEnabled_; }
	void SetDrawDebugEnabled(bool v) { drawDebugEnabled_ = v; }

private:
	CollisionManager() = default;
	~CollisionManager() = default;
	CollisionManager(const CollisionManager&) = delete;
	CollisionManager& operator=(const CollisionManager&) = delete;

	std::vector<IImGuiEditable*> entities_;
	bool drawDebugEnabled_ = true;
};

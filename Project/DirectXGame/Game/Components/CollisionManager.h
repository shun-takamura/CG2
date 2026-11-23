#pragma once

#include <vector>

class IImGuiEditable;
struct Collider;
namespace CollisionGeometry { struct WorldData; }

/// <summary>
/// 球コライダーの登録・判定を一元管理する（シングルトン）。
/// IImGuiEditable の構築/破棄時に自動的に Register/Unregister される。
/// 毎フレーム Update() で全ペアを評価し、衝突発生時にコールバックを呼ぶ。
/// Debug ビルドでは showDebug が true のコライダーを DebugDraw::Sphere で描画する。
/// シングルトンにする理由：登録されたエンティティの当たり判定を、毎フレーム 1 か所でまとめて行うため。
/// </summary>
class CollisionManager {
public:
	/// <summary>唯一のインスタンスを取得する</summary>
	/// <returns>インスタンス</returns>
	static CollisionManager* GetInstance();

	/// <summary>判定の対象に登録する（登録済み・nullptr なら何もしない）</summary>
	/// <param name="e">登録するエンティティ</param>
	void Register(IImGuiEditable* e);
	/// <summary>判定の対象から外す</summary>
	/// <param name="e">外すエンティティ</param>
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

	/// <summary>
	/// 判定の中心・軸。ゲームが姿勢を上書きしていればそれ（Gameplay の SetColliderPose）、
	/// 無ければエンジンの TryGetWorldData（translate＋offset / rotate）。
	/// </summary>
	/// <param name="e">エンティティ</param>
	/// <param name="c">エンティティのコライダー</param>
	/// <param name="out">判定の中心・軸の書き込み先</param>
	/// <returns>取れたら true</returns>
	static bool GetWorldData(IImGuiEditable* e, const Collider& c, CollisionGeometry::WorldData& out);

	//====================
	// グローバル設定（Debug 用）
	//====================

	/// <summary>
	/// すべてのコライダーのデバッグ描画を一括 ON/OFF。デフォルト ON。
	/// 個別の collider.showDebug が false でもこの全体スイッチが OFF ならどこも描画しない。
	/// </summary>
	/// <returns>描画するなら true</returns>
	bool IsDrawDebugEnabled() const { return drawDebugEnabled_; }
	/// <summary>すべてのコライダーのデバッグ描画を一括 ON/OFF する</summary>
	/// <param name="v">描画するなら true</param>
	void SetDrawDebugEnabled(bool v) { drawDebugEnabled_ = v; }

private:
	CollisionManager() = default;
	~CollisionManager() = default;
	CollisionManager(const CollisionManager&) = delete;
	CollisionManager& operator=(const CollisionManager&) = delete;

	std::vector<IImGuiEditable*> entities_;
	bool drawDebugEnabled_ = true;
};

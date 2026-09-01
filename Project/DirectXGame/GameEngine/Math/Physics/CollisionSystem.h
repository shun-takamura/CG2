#pragma once

#include <vector>

#include "Collider.h"

class IImGuiEditable;

/// <summary>
/// ホスト（ゲーム）が注入する衝突ルール（依存性の逆転）。
/// エンジンはレイヤーを「ただの整数」としてしか知らない。
/// 「Player の弾は Player に当たらない」といった意味付けは全部ホスト側の責務。
/// 未配線でも動く（レイヤー 0 扱い・全ペア判定・ヒット時は onCollision のみ）。
/// </summary>
struct CollisionHostHooks {
	/// エンティティのレイヤー番号を返す（ゲームのタグ enum を int にキャストするのが典型）
	int (*getLayer)(IImGuiEditable* e) = nullptr;

	/// レイヤーの組み合わせで判定するか。順序非依存で実装すること
	bool (*shouldCollide)(int layerA, int layerB) = nullptr;

	/// 衝突が成立したときに呼ばれる（ダメージ適用など、ゲーム側の結果処理）
	void (*onHit)(IImGuiEditable* a, IImGuiEditable* b) = nullptr;

	/// デバッグ描画のレイヤー別カラー。未配線なら既定色（緑）
	void (*getLayerColor)(int layer, float& r, float& g, float& b, float& a) = nullptr;
};

/// <summary>
/// コライダーの登録・総当たり判定・デバッグ描画を行う汎用システム（シングルトン）。
///
/// コライダーはエンティティポインタをキーにしたサイドテーブルで保持するので、
/// エンティティ基底クラスに手を入れる必要がない。
///
/// 使い方:
///   1. 起動時に CollisionSystem::SetHostHooks() でゲームのルールを配線
///   2. エンティティ生成時に Register()、破棄時に Unregister()
///   3. ColliderOf(e) で形状を設定し enabled = true
///   4. 毎フレーム Update() を呼ぶ
///
/// 判定アルゴリズム自体は CollisionGeometry に分離してあるので、
/// 独自のブロードフェーズを組みたい場合はそちらを直接使ってもよい。
/// </summary>
class CollisionSystem {
public:
	static CollisionSystem* GetInstance();

	/// <summary>ゲーム側の衝突ルールを配線する。</summary>
	static void SetHostHooks(const CollisionHostHooks& hooks);

	void Register(IImGuiEditable* e);
	void Unregister(IImGuiEditable* e);

	/// <summary>エンティティのコライダーを取得する（無ければ既定値で生成）。</summary>
	Collider& ColliderOf(IImGuiEditable* e);

	/// <summary>登録済みかどうか（コライダーを生成せずに調べる）。</summary>
	bool Has(IImGuiEditable* e) const;

	/// <summary>
	/// 全ペアの衝突判定を実施する。毎フレーム呼ぶこと。
	/// Debug ビルドでは末尾で DrawDebug() も呼ぶ。
	/// </summary>
	void Update();

	/// <summary>
	/// コライダーを線描画キューに積む。Update から自動で呼ばれるが、
	/// 外から強制描画したい場合のために公開しておく。
	/// </summary>
	void DrawDebug();

	/// <summary>登録を全消去する（シーン切り替え時など）。</summary>
	void Clear();

	//====================
	// グローバル設定（Debug 用）
	//====================

	/// <summary>
	/// すべてのコライダーのデバッグ描画を一括 ON/OFF。デフォルト ON。
	/// 個別の collider.showDebug が true でもこの全体スイッチが OFF ならどこも描画しない。
	/// </summary>
	bool IsDrawDebugEnabled() const { return drawDebugEnabled_; }
	void SetDrawDebugEnabled(bool v) { drawDebugEnabled_ = v; }

private:
	CollisionSystem() = default;
	~CollisionSystem() = default;
	CollisionSystem(const CollisionSystem&) = delete;
	CollisionSystem& operator=(const CollisionSystem&) = delete;

	// 登録順を保つため vector。要素数はシーン内エンティティ規模なので線形探索で足りる
	std::vector<IImGuiEditable*> entities_;

	bool drawDebugEnabled_ = true;

	static CollisionHostHooks hostHooks_;
};

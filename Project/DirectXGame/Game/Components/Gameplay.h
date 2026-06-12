#pragma once
#include <memory>
#include "GameplayComponents.h"

class IImGuiEditable;

/// <summary>
/// エンティティ（IImGuiEditable）→ GameplayComponents の登録表（サイドテーブル）。
/// エンジンのエンティティ基底に戦闘データを持たせず、ゲーム側でポインタをキーに保持する。
/// 生成時に Of() でエントリ確保、破棄時に Remove()（IImGuiEditable の dtor フックから呼ぶ）。
/// </summary>
namespace Gameplay {

	/// <summary>エンティティに紐づくコンポーネントを返す（無ければ生成）。</summary>
	GameplayComponents& Of(const IImGuiEditable* e);

	/// <summary>unique_ptr 受け（back / ループ変数などをそのまま渡せるように）。</summary>
	template <class T>
	GameplayComponents& Of(const std::unique_ptr<T>& p) { return Of(p.get()); }

	/// <summary>エンティティのコンポーネントを破棄する。</summary>
	void Remove(const IImGuiEditable* e);

	/// <summary>全エントリ破棄（シーン全消去など）。</summary>
	void Clear();

} // namespace Gameplay

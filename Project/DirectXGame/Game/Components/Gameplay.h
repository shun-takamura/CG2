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
	/// <param name="e">エンティティ（キーとして使うだけ）</param>
	/// <returns>コンポーネント（登録表の中身への参照）</returns>
	GameplayComponents& Of(const IImGuiEditable* e);

	/// <summary>unique_ptr 受けの Of（back / ループ変数などをそのまま渡せるように）。</summary>
	/// <param name="p">エンティティを持つ unique_ptr</param>
	/// <returns>コンポーネント（登録表の中身への参照）</returns>
	template <class T>
	GameplayComponents& Of(const std::unique_ptr<T>& p) { return Of(p.get()); }

	/// <summary>ダメージの持ち主を返す（部位なら本体、それ以外は自分自身）。多段ヒットの重複判定のキーに使う。</summary>
	/// <param name="e">エンティティ</param>
	/// <returns>ダメージの持ち主。e が nullptr なら nullptr</returns>
	inline IImGuiEditable* DamageRootOf(IImGuiEditable* e) {
		if (!e) return nullptr;
		IImGuiEditable* owner = Of(e).GetDamageOwner();
		return owner ? owner : e;
	}

	/// <summary>エンティティのコンポーネントを破棄する。</summary>
	/// <param name="e">破棄するエンティティ</param>
	void Remove(const IImGuiEditable* e);

	/// <summary>全エントリ破棄（シーン全消去など）。</summary>
	void Clear();

} // namespace Gameplay

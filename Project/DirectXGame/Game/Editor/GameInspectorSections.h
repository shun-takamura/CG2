#pragma once

/// <summary>
/// Inspector に差し込むゲーム固有のコンポーネント欄。
///
/// エンジンの InspectorWindow は共通部（名前 / 型 / グループ / 表示 / コライダー）だけを持つ。
/// HP・ダメージ・弾・近接・プレハブといったこのゲーム専用の項目は、
/// ImGuiManager::AddInspectorSection() 経由でここから登録する（依存性の逆転）。
/// </summary>
namespace GameInspectorSections {

	/// <summary>ImGuiManager へセクションを登録する。ImGui 初期化後に一度だけ呼ぶ。</summary>
	void Register();

} // namespace GameInspectorSections

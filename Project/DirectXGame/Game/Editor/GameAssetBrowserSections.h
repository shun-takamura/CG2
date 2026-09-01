#pragma once

/// <summary>
/// SceneEditor（アセットブラウザ）に差し込むゲーム固有のセクション。
///
/// エンジンの SceneEditorWindow は Resources/ を走査して並べる汎用ブラウザだけを持つ。
/// プレハブ一覧やスプライン追加ボタンのように、ゲームの型（PrefabManager / EntityTag）に
/// 触るものは ImGuiManager::AddAssetBrowserSection() 経由でここから登録する。
/// </summary>
namespace GameAssetBrowserSections {

	/// <summary>ImGuiManager へセクションを登録する。ImGui 初期化後に一度だけ呼ぶ。</summary>
	void Register();

} // namespace GameAssetBrowserSections

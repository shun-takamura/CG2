#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Prefab.h"

/// <summary>
/// Resources/Json/Prefabs/ 配下の .json プリファブをロード・キャッシュする（シングルトン）。
/// SceneEditor から Rescan() を呼んで一覧を取り直す。
/// シングルトンにする理由：プレハブの定義（JSON）を一度だけ読み、全てのシーンで共有するため。
/// </summary>
class PrefabManager {
public:
	/// <summary>唯一のインスタンスを取得する</summary>
	/// <returns>インスタンス</returns>
	static PrefabManager* GetInstance();

	/// <summary>
	/// Resources/Json/Prefabs/ をスキャンして全プリファブを読み込み直す
	/// </summary>
	void Rescan();

	/// <summary>
	/// 名前でプリファブを取得
	/// </summary>
	/// <param name="name">プリファブ名</param>
	/// <returns>プリファブ。存在しなければ nullptr</returns>
	const PrefabDef* Find(const std::string& name) const;

	/// <summary>
	/// 全プリファブのリスト
	/// </summary>
	/// <returns>読み込んだプリファブの一覧</returns>
	const std::vector<PrefabDef>& GetAll() const { return prefabs_; }

	/// <summary>
	/// プリファブを JSON にシリアライズしてファイル保存（Inspector の "Save as Prefab" 用）。
	/// </summary>
	/// <param name="def">保存するプリファブ</param>
	/// <param name="filePath">保存先のファイルパス</param>
	/// <returns>保存できたら true</returns>
	static bool Save(const PrefabDef& def, const std::string& filePath);

	/// <summary>
	/// 名前を指定してプリファブの .json ファイルを削除する。
	/// 呼び出し側で Rescan() して一覧を更新すること。
	/// </summary>
	/// <param name="name">プリファブ名（拡張子なし）</param>
	/// <returns>消せたら true。ファイルが無い・消せなければ false</returns>
	static bool Delete(const std::string& name);

	/// <summary>
	/// プリファブ保存先ディレクトリ
	/// </summary>
	/// <returns>"Resources/Json/Prefabs"</returns>
	static const char* GetPrefabDir();

private:
	PrefabManager() = default;
	~PrefabManager() = default;
	PrefabManager(const PrefabManager&) = delete;
	PrefabManager& operator=(const PrefabManager&) = delete;

	/// <summary>プリファブの .json を 1 つ読んで PrefabDef にする</summary>
	/// <param name="filePath">読むファイルパス</param>
	/// <param name="out">読んだ結果の書き込み先</param>
	/// <returns>読めたら true。パースに失敗したら false</returns>
	bool LoadFile(const std::string& filePath, PrefabDef& out) const;

	std::vector<PrefabDef> prefabs_;
};

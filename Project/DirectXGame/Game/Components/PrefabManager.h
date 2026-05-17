#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Prefab.h"

/// <summary>
/// Resources/Json/Prefabs/ 配下の .json プリファブをロード・キャッシュする（シングルトン）。
/// SceneEditor から Rescan() を呼んで一覧を取り直す。
/// </summary>
class PrefabManager {
public:
	static PrefabManager* GetInstance();

	/// <summary>
	/// Resources/Json/Prefabs/ をスキャンして全プリファブを読み込み直す
	/// </summary>
	void Rescan();

	/// <summary>
	/// 名前でプリファブを取得（存在しなければ nullptr）
	/// </summary>
	const PrefabDef* Find(const std::string& name) const;

	/// <summary>
	/// 全プリファブのリスト
	/// </summary>
	const std::vector<PrefabDef>& GetAll() const { return prefabs_; }

	/// <summary>
	/// プリファブを JSON にシリアライズしてファイル保存（Inspector の "Save as Prefab" 用）。
	/// </summary>
	static bool Save(const PrefabDef& def, const std::string& filePath);

	/// <summary>
	/// プリファブ保存先ディレクトリ
	/// </summary>
	static const char* GetPrefabDir();

private:
	PrefabManager() = default;
	~PrefabManager() = default;
	PrefabManager(const PrefabManager&) = delete;
	PrefabManager& operator=(const PrefabManager&) = delete;

	bool LoadFile(const std::string& filePath, PrefabDef& out) const;

	std::vector<PrefabDef> prefabs_;
};

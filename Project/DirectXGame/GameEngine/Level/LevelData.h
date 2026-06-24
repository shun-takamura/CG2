#pragma once

#include <string>
#include <vector>

#include "Vector3.h"

/// <summary>
/// Blenderレベルエディタが出力したJSONを読み込んだシーン配置用データ。
/// 描画やゲームロジックには依存しない純粋なデータ構造。
/// 実際のオブジェクト生成・配置はこれを受け取ったシーン側で行う。
/// </summary>
struct LevelData {

	/// <summary>
	/// オブジェクト1個分のデータ（子を再帰的に持つツリー構造）
	/// </summary>
	struct ObjectData {
		std::string type;  // オブジェクト種類（"MESH" など）
		std::string name;  // オブジェクト名

		// トランスフォーム（ゲーム座標系に変換済み）
		// 回転は度数法（Blenderエクスポート値そのまま）。
		// エンジンがラジアンを要求する場合は利用側で変換すること。
		Vector3 translation{ 0.0f, 0.0f, 0.0f };
		Vector3 rotation{ 0.0f, 0.0f, 0.0f };
		Vector3 scaling{ 1.0f, 1.0f, 1.0f };

		// カスタムプロパティ file_name（無ければ空文字）
		std::string fileName;

		// コライダー（collider カスタムプロパティがある場合のみ有効）
		bool hasCollider = false;
		std::string colliderType;  // "BOX" など
		Vector3 colliderCenter{ 0.0f, 0.0f, 0.0f };
		Vector3 colliderSize{ 0.0f, 0.0f, 0.0f };

		// 子オブジェクト
		std::vector<ObjectData> children;
	};

	std::string name;                 // ルートノード名（"scene"）
	std::vector<ObjectData> objects;  // ルート直下のオブジェクト
};

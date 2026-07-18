#pragma once

#include <string>
#include <vector>

#include "Vector2.h"
#include "Vector3.h"
#include "Components/EntityTag.h"

/// <summary>
/// シーン JSON の 1 エントリを表すただの記述子。
/// Object3DInstance などの実体とは無関係で、どのシーンにも属さない。
/// </summary>
struct SceneEntityDesc {
	enum class Kind {
		Object3D,          // 静的モデル（dir + file）
		AnimatedObject3D,  // スキンモデル（dir + file）
		Primitive,         // プリミティブ（primitiveType + texture）
		Sprite,            // 2D スプライト（texture + spritePos）
		Spline,            // 制御点列（points）
		Prefab,            // プレハブ由来（prefabName）。HP/弾等はプレハブ定義から復元する
	};

	Kind        kind = Kind::Object3D;
	std::string name;
	EntityTag   tag = EntityTag::None;

	// Object3D / AnimatedObject3D
	std::string dir;
	std::string file;

	// Prefab
	std::string prefabName;

	// 3D 共通のトランスフォーム
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	Vector3 rotate{};
	Vector3 translate{};

	// Primitive
	int         primitiveType = 0;
	std::string texture;

	// Sprite
	Vector2 spritePos{};

	// Spline
	std::vector<Vector3> points;
};

/// <summary>シーン 1 枚分の記述。</summary>
struct SceneData {
	std::string sceneName;                  // JSON の "scene" フィールド
	std::vector<SceneEntityDesc> entities;  // JSON の "objects" 配列
};

/// <summary>
/// シーン JSON の読み書き **だけ** を受け持つ。
///
/// シーンやエンティティを一切知らないので、
///   ・ゲームを起動せずに単体で試せる
///   ・JSON の形（キー名・型・新エントリ種別）の変更がこのファイルだけで閉じる
/// のが狙い。Blender 連携でシーン JSON の形は今後も増えるため、
/// 「形を知っている場所」を 1 箇所に閉じ込めておく。
///
/// 実体との対応付け（コンテナからの収集 / コンテナへの復元）は GameScene 側が持つ。
/// </summary>
namespace SceneSerializer {

	/// <summary>SceneData を JSON ファイルへ書き出す。失敗時 false。</summary>
	bool WriteFile(const std::string& filePath, const SceneData& data);

	/// <summary>SceneData を JSON 文字列にする（ファイルには書かない）。差分検出のハッシュ源に使う。</summary>
	std::string ToString(const SceneData& data);

	/// <summary>
	/// JSON ファイルを SceneData へ読み込む。失敗時 false（out は変更しない）。
	/// **パースに失敗しても呼び出し側のシーンは壊さない**のが約束（先に読み切ってから差し替える）。
	/// </summary>
	bool ReadFile(const std::string& filePath, SceneData& out, std::string* errorMessage = nullptr);

}  // namespace SceneSerializer

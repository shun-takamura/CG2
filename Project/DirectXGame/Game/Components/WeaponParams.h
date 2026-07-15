#pragma once

#include <string>
#include "Vector3.h"

/// <summary>
/// プレイヤープレハブ用の武器（ボーンソケット追従）パラメータ。
/// Inspector（Player 選択時）で編集 → "Save as Prefab" で player.json の weapon ブロックに反映される。
/// modelDir/modelFile は Initialize で1度ロードする（実行中の差し替えは非対応）。
/// </summary>
struct WeaponParams {
	bool        enabled = true;
	std::string bone;                              // 追従先ボーン名（例: "mixamorig:RightHand"）
	Vector3     offsetTranslate{ 0.0f, 0.0f, 0.0f }; // 握りオフセット（ボーンローカル）
	Vector3     offsetRotate{ 0.0f, 0.0f, 0.0f };
	Vector3     offsetScale{ 1.0f, 1.0f, 1.0f };
	std::string modelDir;                          // 武器モデルのディレクトリ
	std::string modelFile;                         // 武器モデルのファイル
};

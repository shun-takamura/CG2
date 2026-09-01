#pragma once

// 名前は歴史的に SphereCollider.h のままだが、実体はエンジンの汎用コライダー（Collider）。
// 型定義は GameEngine/Math/Physics/Collider.h へ移設済み（エンジンとゲームで同じ型を共有する）。
// （ファイル名のリネームは vcxproj/include の影響が広いため後回し）

#include "Physics/Collider.h"

// 旧名互換（マクロ衝突や旧コード互換のため当面残す）
using SphereCollider = Collider;

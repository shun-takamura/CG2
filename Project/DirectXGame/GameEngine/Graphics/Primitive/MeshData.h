#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <vector>
#include <cstdint>

// プリミティブ用の頂点構造体
struct MeshVertex {
    Vector3 position;
    Vector2 texcoord;
    Vector3 normal;
    Vector4 color;
};

// メッシュデータ（頂点 + インデックス）
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};
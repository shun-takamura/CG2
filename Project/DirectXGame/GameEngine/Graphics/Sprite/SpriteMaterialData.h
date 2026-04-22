#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"

struct SpriteMaterialData {
    Vector4 color;         // offset 0
    Matrix4x4 uvTransform; // offset 16 (自動でアラインされる)
};
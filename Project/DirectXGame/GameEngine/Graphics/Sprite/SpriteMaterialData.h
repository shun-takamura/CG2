#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"

struct SpriteMaterialData {
    Vector4 color;
    float padding[3];
    Matrix4x4 uvTransform;
};
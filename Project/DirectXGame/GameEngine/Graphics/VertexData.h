#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
	Vector4 tangent;  // xyz=接線, w=handedness(±1)。法線マップ用
};



#pragma once
#include "Vector4.h"

typedef struct Vector2 {
	float x;
	float y;
}Vector2;

struct VertexData {
	Vector4 pos;
	Vector2 texcoord;
	Vector3 normal;
};

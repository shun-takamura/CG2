#pragma once
#include "Vector4.h"
#include "Matrix3x3.h"


typedef struct Material {
	Vector4 color;
	int enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
}Material;
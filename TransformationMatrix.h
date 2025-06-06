#pragma once
#include "Matrix4x4.h"

typedef struct TransformationMatrix {
	Matrix4x4 wvp;
	Matrix4x4 world;
}TransformationMatrix;
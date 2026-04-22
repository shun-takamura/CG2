#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"

typedef struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
	float shininess;
	float environmentCoefficient;
	int32_t useEnvironmentMap;  // 環境マップの使用ON/OFF
	float padding2;
}Material;


struct MaterialData
{
	std::string textureFilePath;
	uint32_t textureIndex = 0;
};
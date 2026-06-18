#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"
#include "AssetLocator.h"
#include <cstring>
#include <string>

typedef struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
	float shininess;
	float environmentCoefficient;
	int32_t useEnvironmentMap;  // 環境マップの使用ON/OFF
	float metallic;             // PBR: 金属度 0..1
	float roughness;            // PBR: 粗さ 0..1
	int32_t shadingModel;       // 0=BlinnPhong, 1=PBR（PSO 選択に使用）
	int32_t useNormalMap;       // 1=法線マップ(t2)を適用, 0=ジオメトリ法線
	float padding2;
}Material;


struct MaterialData
{
	std::string textureFilePath;
	std::string normalMapFilePath;   // 法線マップ DDS パス（空＝なし）
	uint32_t textureIndex = 0;
};

// .mat v1 を読み出して MaterialData と Material 構造体（GPU 定数バッファ）に流し込む。
// Material* out_gpu_material が nullptr でなければ GPU 用パラメータも上書きする。
// 成功時 true。
inline bool LoadMatFile(const std::string& matPath,
                        MaterialData& out_data,
                        Material* out_gpu_material = nullptr)
{
	auto h = AssetLocator::GetInstance()->Open(matPath);
	if (!h.IsValid()) return false;

	char magic[4]{};
	h.Read(magic, 4);
	if (std::memcmp(magic, "MATL", 4) != 0) return false;

	uint32_t version = 0;
	h.Read(&version, 4);
	if (version < 1 || version > 3) return false;

	char baseColorPath[256]{};
	h.Read(baseColorPath, 256);
	out_data.textureFilePath = std::string(baseColorPath);

	float color[4]{};
	int32_t enableLighting = 0;
	float shininess = 0.0f;
	float envCoeff = 0.0f;
	int32_t useEnvMap = 0;
	h.Read(color, sizeof(color));
	h.Read(&enableLighting, 4);
	h.Read(&shininess, 4);
	h.Read(&envCoeff, 4);
	h.Read(&useEnvMap, 4);

	// v2 で追加された PBR パラメータ（v1 はデフォルト補完）
	float metallic = 0.0f;
	float roughness = 0.5f;
	int32_t shadingModel = 0;
	if (version >= 2) {
		h.Read(&metallic, 4);
		h.Read(&roughness, 4);
		h.Read(&shadingModel, 4);
	}

	// v3 で追加された法線マップパス
	if (version >= 3) {
		char normalMapPath[256]{};
		h.Read(normalMapPath, 256);
		out_data.normalMapFilePath = std::string(normalMapPath);
	}

	if (out_gpu_material) {
		out_gpu_material->color = Vector4(color[0], color[1], color[2], color[3]);
		out_gpu_material->enableLighting = enableLighting;
		out_gpu_material->shininess = shininess;
		out_gpu_material->environmentCoefficient = envCoeff;
		out_gpu_material->useEnvironmentMap = useEnvMap;
		out_gpu_material->metallic = metallic;
		out_gpu_material->roughness = roughness;
		out_gpu_material->shadingModel = shadingModel;
		out_gpu_material->useNormalMap = out_data.normalMapFilePath.empty() ? 0 : 1;
	}
	return true;
}
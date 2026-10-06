#pragma once
#include "Vector3.h"
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
	float cloudReflection;      // PBR: 鏡面反射に映す遠景の雲の強さ（0=映さない。CloudSky.hlsli）

	// ディゾルブ（Object3D/Dissolve.hlsli）。マスクは「ワールドの高さ」と「ワールド座標の 3D ノイズ」を混ぜてシェーダで作る
	int32_t dissolveEnable;     // 0=無効（既定）
	float dissolveProgress;     // 見えている割合 0..1（0=全部消えている / 1=全部見えている）
	float dissolveEdgeWidth;    // 境目の発光帯の幅（マスク値の単位）
	float dissolveNoiseScale;   // ノイズの細かさ [1/m]
	float dissolveHeightMin;    // 高さの範囲（ワールド Y）。下端から順に現れる
	float dissolveHeightMax;
	float dissolveNoiseWeight;  // 0=高さだけ（水平に切れる） / 1=ノイズだけ
	float dissolvePadding;
	Vector3 dissolveEdgeColor;  // 境目の発光色
	float dissolvePadding2;

	// 視差オクルージョン（POM。Object3dPBR.PS のみが読む）。ハイトマップは t4
	int32_t useParallax;        // 1=ハイトマップ(t4)で UV をずらす
	float parallaxDepth;        // 凹凸の深さ（UV 1 あたり。実寸の深さ[m] ÷ UV 1 の長さ[m]）
	float parallaxMinLayers;    // 正面から見たときのレイマーチの段数
	float parallaxMaxLayers;    // 浅い角度から見たときの段数
}Material;
// HLSL 側（Object3d*.PS.hlsl の Material）とレイアウトを合わせる。定数バッファは 256B 単位なので確保量は変わらない。
// 末尾の視差は PBR の PS だけが宣言する（他の PS は手前までしか読まないので宣言しなくてよい）
static_assert(sizeof(Material) == 192, "Material のレイアウトを変えたら Object3D の PS 側も合わせること");


struct MaterialData
{
	std::string textureFilePath;
	std::string normalMapFilePath;   // 法線マップ DDS パス（空＝なし）
	std::string heightMapFilePath;   // ハイトマップ DDS パス（.mat v4。空＝視差なし）
	uint32_t textureIndex = 0;
};

// .mat v1〜v4 を読み出して MaterialData と Material 構造体（GPU 定数バッファ）に流し込む。
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
	if (version < 1 || version > 4) return false;

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

	// v4 で追加された視差（ハイトマップと深さ）
	float parallaxDepth = 0.0f;
	if (version >= 4) {
		char heightMapPath[256]{};
		h.Read(heightMapPath, 256);
		out_data.heightMapFilePath = std::string(heightMapPath);
		h.Read(&parallaxDepth, 4);
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
		// ハイトマップの読み込みに失敗したら呼び出し側で 0 に戻す
		out_gpu_material->useParallax = out_data.heightMapFilePath.empty() ? 0 : 1;
		out_gpu_material->parallaxDepth = parallaxDepth;
	}
	return true;
}
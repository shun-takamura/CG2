#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "AssetLocator.h"
#include <cstdint>
#include <cstring>
#include <string>

// Material::shadingModel の値（.mat に保存される数字なので、値を変えない）
namespace ShadingModel {
	inline constexpr int32_t kBlinnPhong = 0;  // Blinn-Phong（既定）
	inline constexpr int32_t kPBR = 1;         // PBR（Cook-Torrance）
	inline constexpr int32_t kTerrain = 2;     // 地形（スプラット＋3 層）。静的なモデルだけ
}

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
	int32_t shadingModel;       // ShadingModel::k…（PSO 選択に使用。Object3DManager::SelectShaderType）
	int32_t useNormalMap;       // 1=法線マップ(t2)を適用, 0=ジオメトリ法線
	float cloudReflection;      // PBR: 鏡面反射に映す遠景の雲の強さ（0=映さない。CloudSky.hlsli）

	// ディゾルブ（Object3D/Dissolve.hlsli）。マスクは「ワールドの高さ」と「ワールド座標の 3D ノイズ」を混ぜてシェーダで作る
	int32_t dissolveEnable;     // 0=無効（既定） / 1=高さ＋ノイズ / 2=画面座標の六角形（kDissolveHexScreen。下の欄の読み替えは Dissolve.hlsli）
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

	// 地形（.mat v5。Terrain.PS のみが読む）。層の順番は 草 / 土 / 岩 = スプラットマップの R / G / B
	Vector4 terrainTile;        // xyz = 層ごとのタイル長 [m]、w = 高さブレンドの幅
	Vector4 terrainRoughness;   // xyz = 層ごとの粗さ、w = 岩の三平面投影の鋭さ
	Vector4 terrainParams;      // x = 低い周波数の色ムラの強さ、y = 濡れた所を暗くする強さ、zw = 予備
	// 発光（全シェーダ共通）。rgb = 発光の色×強さ（ライティングのあとに足す。既定 0）、w は予備。
	// 光らせる（ブルーム）には Object3DInstance::DrawBloomPass で発光 RT にも描く
	Vector4 emissive;
}Material;

// Material::dissolveEnable の値
inline constexpr int32_t kDissolveOff = 0;
inline constexpr int32_t kDissolveWorld = 1;       // ワールドの高さ＋3D ノイズ（既定のディゾルブ）
inline constexpr int32_t kDissolveHexScreen = 2;   // 画面座標の六角形が中心から晴れる
inline constexpr int32_t kDissolveSphereHole = 3;  // 点からの距離：半径の内側が消える（胸の装甲に入口を開ける）
inline constexpr int32_t kDissolveSphereGrow = 4;  // 点からの距離：半径の内側だけが見える（羽根を付け根から出す・先から消す）
inline constexpr int32_t kDissolveHexHole = 5;     // ワールドの六角形（+Z を向いた面）：中心から順にマスごと消える（胸の装甲に入口を開ける）
inline constexpr int32_t kDissolveRise = 6;        // 高さの面：面より下が消える。面を上げると下から上へほどける（死亡演出のエーテル化）

// 点からの距離のディゾルブ（kDissolveSphereHole / Grow / kDissolveHexHole）の設定。Material の空いている欄を読み替えて渡す（Dissolve.hlsli）
struct DissolveSphere {
	int32_t mode = kDissolveSphereHole;
	Vector3 center = { 0.0f, 0.0f, 0.0f };   // 中心（ワールド）
	float radius = 0.0f;                     // 半径 [m]
	float stretchY = 1.0f;                   // 縦の伸び（1=球）
	float edgeWidth = 0.15f;                 // 光る縁の幅 [m]（残る側に付く）
	Vector3 edgeColor = { 1.0f, 1.0f, 1.0f };// 光る縁の色（1 を超えてよい。発光パスでブルームに乗る）
	float noiseScale = 2.0f;                 // 縁のゆらぎの細かさ [1/m]
	float noiseAmplitude = 0.0f;             // 縁のゆらぎの振れ幅 [m]
	// kDissolveHexHole だけ（noise* の代わりに使う。edgeWidth は光る帯の幅）
	float cellSize = 0.35f;                  // 六角形の大きさ [m]
	float cellJitter = 0.3f;                 // マスごとのばらつき [m]
};

/// <summary>点からの距離のディゾルブの設定を、Material の空いている欄に読み替えて書き込む（中心は heightMin/Max/padding へ）</summary>
/// <param name="m">書き込む先のマテリアル</param>
/// <param name="d">ディゾルブの設定（六角形のときは noise の代わりに cellSize / cellJitter を使う）</param>
inline void SetDissolveSphere(Material& m, const DissolveSphere& d)
{
	m.dissolveEnable = d.mode;
	m.dissolveHeightMin = d.center.x;
	m.dissolveHeightMax = d.center.y;
	m.dissolvePadding = d.center.z;
	m.dissolveProgress = d.radius;
	m.dissolvePadding2 = d.stretchY;
	m.dissolveEdgeWidth = d.edgeWidth;
	m.dissolveEdgeColor = d.edgeColor;
	const bool hex = d.mode == kDissolveHexHole;
	m.dissolveNoiseScale = hex ? d.cellSize : d.noiseScale;
	m.dissolveNoiseWeight = hex ? d.cellJitter : d.noiseAmplitude;
}

// ワールド座標のノイズだけのディゾルブ（kDissolveWorld を高さ抜きで使う）。場所の順番なしにまだらに欠ける（羽根の焼け落ち）
struct DissolveNoise {
	float progress = 1.0f;                   // 見えている割合 0..1（0=全部消えた / 1=全部見えている）
	float edgeWidth = 0.1f;                  // 光る縁の幅（ノイズ値 0..1 の単位）
	Vector3 edgeColor = { 1.0f, 1.0f, 1.0f };// 光る縁の色（1 を超えてよい。発光パスでブルームに乗る）
	float noiseScale = 4.0f;                 // ノイズの細かさ [1/m]
};

/// <summary>ワールド座標のノイズだけのディゾルブ（kDissolveWorld を高さ 0〜1・ノイズの重み 1 で）をマテリアルに書き込む</summary>
/// <param name="m">書き込む先のマテリアル</param>
/// <param name="d">ディゾルブの設定</param>
inline void SetDissolveNoise(Material& m, const DissolveNoise& d)
{
	m.dissolveEnable = kDissolveWorld;
	m.dissolveProgress = d.progress;
	m.dissolveEdgeWidth = d.edgeWidth;
	m.dissolveEdgeColor = d.edgeColor;
	m.dissolveNoiseScale = d.noiseScale;
	m.dissolveHeightMin = 0.0f;
	m.dissolveHeightMax = 1.0f;
	m.dissolveNoiseWeight = 1.0f;
}

// 高さの面のディゾルブ（kDissolveRise）の設定。面はワールドの水平面で、面より下が消える（Dissolve.hlsli の DissolveRise）
struct DissolvePlane {
	float height = 0.0f;                     // 面の高さ（ワールド Y）[m]
	float edgeWidth = 0.1f;                  // 光る縁の幅 [m]（面の上の残る側に付く）
	Vector3 edgeColor = { 1.0f, 1.0f, 1.0f };// 光る縁の色（1 を超えてよい。発光パスでブルームに乗る）
	float noiseScale = 4.0f;                 // 縁のゆらぎの細かさ [1/m]
	float noiseAmplitude = 0.05f;            // 縁のゆらぎの振れ幅 [m]
};

/// <summary>高さの面のディゾルブ（kDissolveRise）をマテリアルに書き込む（面の高さは dissolveProgress へ）</summary>
/// <param name="m">書き込む先のマテリアル</param>
/// <param name="d">ディゾルブの設定</param>
inline void SetDissolvePlane(Material& m, const DissolvePlane& d)
{
	m.dissolveEnable = kDissolveRise;
	m.dissolveProgress = d.height;
	m.dissolveEdgeWidth = d.edgeWidth;
	m.dissolveEdgeColor = d.edgeColor;
	m.dissolveNoiseScale = d.noiseScale;
	m.dissolveNoiseWeight = d.noiseAmplitude;
}
// HLSL 側（Object3d*.PS.hlsl の Material）とレイアウトを合わせる。定数バッファは 256B 単位なので確保量は変わらない。
// 末尾の視差は PBR / 地形の PS、地形の欄は地形の PS だけが宣言する（他の PS は手前までしか読まないので宣言しなくてよい）
static_assert(sizeof(Material) == 256, "Material のレイアウトを変えたら Object3D の PS 側も合わせること");


struct MaterialData
{
	std::string textureFilePath;
	std::string normalMapFilePath;   // 法線マップ DDS パス（空＝なし）
	std::string heightMapFilePath;   // ハイトマップ DDS パス（.mat v4。空＝視差なし）
	std::string terrainColorArrayPath;   // 地形の層の色の配列 DDS（.mat v5。空＝地形でない）
	std::string terrainNormalArrayPath;  // 地形の層の法線の配列 DDS（.mat v5）
	uint32_t textureIndex = 0;
};
// .mat ファイルの形式（LoadMatFile が読む）
inline constexpr int kMatFileMagicSize = 4;              // 先頭のマジック "MATL" の文字数 [byte]
inline constexpr int kMatFileFieldSize = 4;              // 版・数値 1 個の大きさ [byte]
inline constexpr int kMatFilePathLength = 256;           // テクスチャのパス 1 本の固定長 [byte]
inline constexpr uint32_t kMatFileLatestVersion = 5;     // 読める一番新しい版
inline constexpr uint32_t kMatFileVersionPBR = 2;        // 金属度・粗さ・シェーディングモデルが入った版
inline constexpr uint32_t kMatFileVersionNormalMap = 3;  // 法線マップのパスが入った版
inline constexpr uint32_t kMatFileVersionParallax = 4;   // ハイトマップと視差の深さが入った版
inline constexpr uint32_t kMatFileVersionTerrain = 5;    // 地形の層が入った版
// .mat の古い版に無い値の既定値
inline constexpr float kMatFileDefaultRoughness = 0.5f;
inline constexpr int kMatFileTerrainLayerCount = 3;      // 地形の層の数（草 / 土 / 岩）
inline constexpr int kMatFileTerrainExtraCount = 4;      // 地形の追加の値の数（高さブレンド / 三平面の鋭さ / 色ムラ / 濡れ）
inline constexpr float kMatFileDefaultTileGrass = 4.0f;  // 層ごとのタイル長 [m]
inline constexpr float kMatFileDefaultTileSoil = 4.0f;
inline constexpr float kMatFileDefaultTileRock = 8.0f;
inline constexpr float kMatFileDefaultRoughnessGrass = 0.95f;  // 層ごとの粗さ
inline constexpr float kMatFileDefaultRoughnessSoil = 0.9f;
inline constexpr float kMatFileDefaultRoughnessRock = 0.8f;
inline constexpr float kMatFileDefaultHeightBlend = 0.2f;          // 高さブレンドの幅
inline constexpr float kMatFileDefaultTriplanarSharpness = 4.0f;   // 岩の三平面投影の鋭さ
inline constexpr float kMatFileDefaultMacroVariation = 0.25f;      // 低い周波数の色ムラの強さ
inline constexpr float kMatFileDefaultWetness = 0.45f;             // 濡れた所を暗くする強さ

/// <summary>
/// .mat v1〜v5 を読み出して MaterialData と Material 構造体（GPU 定数バッファ）に流し込む。
/// 古い版に無い値は既定値で補う
/// </summary>
/// <param name="matPath">.mat ファイルのパス（AssetLocator で開く）</param>
/// <param name="out_data">テクスチャ類のパスを書き込む先</param>
/// <param name="out_gpu_material">nullptr でなければ GPU 用パラメータも上書きする</param>
/// <returns>成功時 true。開けない・マジックが違う・版が 1〜5 でなければ false</returns>
inline bool LoadMatFile(const std::string& matPath,
                        MaterialData& out_data,
                        Material* out_gpu_material = nullptr)
{
	auto h = AssetLocator::GetInstance()->Open(matPath);
	if (!h.IsValid()) return false;

	char magic[kMatFileMagicSize]{};
	h.Read(magic, kMatFileMagicSize);
	if (std::memcmp(magic, "MATL", kMatFileMagicSize) != 0) return false;

	uint32_t version = 0;
	h.Read(&version, kMatFileFieldSize);
	if (version < 1 || version > kMatFileLatestVersion) return false;

	char baseColorPath[kMatFilePathLength]{};
	h.Read(baseColorPath, kMatFilePathLength);
	out_data.textureFilePath = std::string(baseColorPath);

	float color[4]{};
	int32_t enableLighting = 0;
	float shininess = 0.0f;
	float envCoeff = 0.0f;
	int32_t useEnvMap = 0;
	h.Read(color, sizeof(color));
	h.Read(&enableLighting, kMatFileFieldSize);
	h.Read(&shininess, kMatFileFieldSize);
	h.Read(&envCoeff, kMatFileFieldSize);
	h.Read(&useEnvMap, kMatFileFieldSize);

	// v2 で追加された PBR パラメータ（v1 はデフォルト補完）
	float metallic = 0.0f;
	float roughness = kMatFileDefaultRoughness;
	int32_t shadingModel = 0;
	if (version >= kMatFileVersionPBR) {
		h.Read(&metallic, kMatFileFieldSize);
		h.Read(&roughness, kMatFileFieldSize);
		h.Read(&shadingModel, kMatFileFieldSize);
	}

	// v3 で追加された法線マップパス
	if (version >= kMatFileVersionNormalMap) {
		char normalMapPath[kMatFilePathLength]{};
		h.Read(normalMapPath, kMatFilePathLength);
		out_data.normalMapFilePath = std::string(normalMapPath);
	}

	// v4 で追加された視差（ハイトマップと深さ）
	float parallaxDepth = 0.0f;
	if (version >= kMatFileVersionParallax) {
		char heightMapPath[kMatFilePathLength]{};
		h.Read(heightMapPath, kMatFilePathLength);
		out_data.heightMapFilePath = std::string(heightMapPath);
		h.Read(&parallaxDepth, kMatFileFieldSize);
	}

	// v5 で追加された地形の層（配列テクスチャ 2 本と層ごとのパラメータ）
	float terrainTile[kMatFileTerrainLayerCount]{ kMatFileDefaultTileGrass, kMatFileDefaultTileSoil, kMatFileDefaultTileRock };
	float terrainRoughness[kMatFileTerrainLayerCount]{ kMatFileDefaultRoughnessGrass, kMatFileDefaultRoughnessSoil, kMatFileDefaultRoughnessRock };
	float terrainExtra[kMatFileTerrainExtraCount]{ kMatFileDefaultHeightBlend, kMatFileDefaultTriplanarSharpness, kMatFileDefaultMacroVariation, kMatFileDefaultWetness };  // heightBlend / triplanarSharpness / macroVariation / wetness
	if (version >= kMatFileVersionTerrain) {
		char colorArrayPath[kMatFilePathLength]{};
		char normalArrayPath[kMatFilePathLength]{};
		h.Read(colorArrayPath, kMatFilePathLength);
		h.Read(normalArrayPath, kMatFilePathLength);
		out_data.terrainColorArrayPath = std::string(colorArrayPath);
		out_data.terrainNormalArrayPath = std::string(normalArrayPath);
		h.Read(terrainTile, sizeof(terrainTile));
		h.Read(terrainRoughness, sizeof(terrainRoughness));
		h.Read(terrainExtra, sizeof(terrainExtra));
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
		out_gpu_material->terrainTile = Vector4(terrainTile[0], terrainTile[1], terrainTile[2], terrainExtra[0]);
		out_gpu_material->terrainRoughness = Vector4(terrainRoughness[0], terrainRoughness[1], terrainRoughness[2], terrainExtra[1]);
		out_gpu_material->terrainParams = Vector4(terrainExtra[2], terrainExtra[3], 0.0f, 0.0f);
	}
	return true;
}
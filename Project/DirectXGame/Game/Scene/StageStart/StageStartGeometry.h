#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <array>
#include <string>
#include <vector>

/// <summary>
/// スタート演出（16_StageStart.md）のリフト・縦穴・カタパルトの寸法。
/// Resources/Json/Tuning/StageStartGeometry.json を読む。この JSON は tools/BlenderPipeline/gen_catapult.py の
/// --export が書き出すもので、**手で直さない**（形を変えるときはスクリプトの定数を直して書き出し直す。16 番 §2.1）。
/// HubScene（③OUT：床の穴・リフトの降下）と StagePlay（③IN：縦穴の続き・カタパルト）の両方が使う。
/// 読めなかった値は既定値（2026-10-15 の F1 の値）のまま。
/// </summary>
struct StageStartGeometry {
	static constexpr const char* kPath = "Resources/Json/Tuning/StageStartGeometry.json";

	// 八角形（正方形の角を落とした形）。リフト・床の穴・縦穴の断面で同じ値
	float octagonHalfWidth = 10.0f;
	float octagonChamfer = 1.0f;

	// リフト（原点＝上面の中心、上面 y=0）
	float liftThickness = 1.2f;
	float liftRimWidth = 0.3f;

	// 縦穴（単位の原点＝下の端の中心。縁の原点＝床の高さの中心で下へ topCollarDepth）
	float shaftUnitHeight = 8.0f;
	float shaftTopCollarDepth = 1.5f;
	float shaftBandY[2] = { 3.78f, 4.22f };

	// カタパルト（原点＝リフトが止まる位置の中心、床 y=0、機体は +Z へ）
	float catapultCeilingY = 25.0f;
	float catapultShaftBaseY = 26.0f;   // 縦穴の単位はここから上へ積む
	float catapultRailStartZ = 10.0f;
	float catapultTunnelEndZ = 160.0f;
	float catapultExitLightZ = 161.0f;
	std::array<Vector3, 2> clamps = { Vector3{ -3.0f, 0.0f, 1.2f }, Vector3{ 3.0f, 0.0f, 1.2f } };
	std::array<Vector3, 2> cableAnchors = { Vector3{ 3.0f, 21.0f, -12.8f }, Vector3{ -3.0f, 21.0f, -12.8f } };
	std::string neonMaterial = "Neon";
	// 斜めの面のネオンの列（機体確認の後に機体の位置から射出口へ 1 列ずつ点け直す。F4）。z は列の中心。
	// 空なら列に分かれていない古い書き出し（全部 neonMaterial で光る）
	std::vector<std::string> neonRowMaterials;
	std::vector<float> neonRowZ;
	std::array<std::string, 2> lcdMaterials = { "LcdLeft", "LcdRight" };
	float lcdUPerMeterZ[2] = { 0.0254629f, -0.0254629f };
	std::string lcdTextureUnable = "Resources/Models/Catapult/LcdLaunchUnable.dds";
	std::string lcdTextureReady = "Resources/Models/Catapult/LcdLaunchReady.dds";

	/// <summary>JSON を読む。ファイルが無い・壊れていれば false（値は既定のまま）</summary>
	bool Load(const std::string& path = kPath);
};

/// <summary>
/// ③ の切れ目で HubScene から StagePlay へ渡す値（16_StageStart.md §4.2）。
/// HubScene が ChangeScene の直前に Store し、StagePlay が最初に Take する（取ったら空になる）。
/// 値が無い（HUB を通らずにステージへ来た）ときは Take が false を返すので、StagePlay は縦穴を飛ばしてよい。
/// </summary>
struct StageStartHandoff {
	float liftY = 0.0f;               // リフトの上面の高さ（HUB の床 y=0 から。負＝下）
	// 縦穴の模様の位相 [m]：リフトの上面が、すぐ下の単位の境目（単位の下の端）から何 m 上にあるか（0..unitHeight）。
	// HUB では単位の下の端は y = −topCollarDepth − unitHeight·(k+1)
	float shaftPhase = 0.0f;
	float descentSpeed = 0.0f;        // 降下の速さ [m/s]（正＝下へ）
	float cockpitLightLevel = 1.0f;   // コクピットの灯り（CockpitReveal の lightLevel）
	float revealAmount = 1.0f;        // 外が透けている度合い（六角形が晴れた＝1）
	float sunScale = 1.0f;            // 外の光の倍率（縦穴の中で弱めた後の値）

	// 切れ目で見え方をそろえるための値（StagePlay はこの値で最初のフレームを描く）
	Vector3 eyeOffset = { 0.0f, 0.0f, 0.0f };   // 目の位置（機体の原点＝リフトの上面の中心から）
	float eyeYaw = 0.0f;              // [rad]
	float eyePitch = 0.0f;            // [rad]
	float fovY = 1.0472f;             // 縦の画角 [rad]
	Vector3 sunDirection = { 0.0f, -1.0f, 0.0f };   // 平行光源（強さは縦穴の中で弱めた後の値）
	Vector4 sunColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	float sunIntensity = 0.0f;
	std::string iblPath;              // 縦穴で使っている IBL（HUB の神殿のもの）

	static void Store(const StageStartHandoff& handoff);
	/// <summary>渡された値があれば out に入れて true（以後は空）</summary>
	static bool Take(StageStartHandoff& out);
	static bool HasPending();
};

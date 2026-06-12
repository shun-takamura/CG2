#pragma once

#include <string>

#include "Vector2.h"
#include "Vector4.h"
#include "PrimitiveGenerator.h" // RingParams / CylinderParams / HelixParams
#include "BillboardMode.h"
#include "TimeGroup.h"

/// <summary>
/// PrimitiveInstance の見た目設定（マテリアル / UV / 形状 / ビルボード / 時間グループ）を
/// まとめたシリアライズ可能なパラメータ。Prefab（ゲーム）やエフェクト定義から
/// PrimitiveInstance::ApplyPrefabParams に流し込む。
/// 参照する型はすべてエンジン側（Vector / PrimitiveGenerator / BillboardMode / TimeGroup）であり、
/// ゲーム固有の概念を含まないため GameEngine 側に置く。
/// </summary>
struct PrimitivePrefabParams {
	int primitiveType = 0; // PrimitiveInstance::PrimitiveType の int 値
	std::string texturePath;

	// マテリアル
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
	int   blendMode = 1;     // PrimitivePipeline::BlendMode (None=0, Normal=1, Add=2, ...)
	bool  depthWrite = true;
	float alphaReference = 0.0f;
	bool  cullBackface = false;
	int   samplerMode = 0;   // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll

	// UV
	bool    uvAutoScroll = false;
	Vector2 uvScrollSpeed{ 0.0f, 0.0f };
	Vector2 uvOffset{ 0.0f, 0.0f };
	Vector2 uvScale{ 1.0f, 1.0f };
	bool    uvFlipU = false;
	bool    uvFlipV = false;

	// ビルボード
	BillboardMode billboardMode = BillboardMode::None;

	// 時間グループ
	TimeGroup timeGroup = TimeGroup::World;

	// 形状パラメータ（種類に応じて使用するものが変わる）
	PrimitiveGenerator::RingParams     ringParams{};
	PrimitiveGenerator::CylinderParams cylinderParams{};
	PrimitiveGenerator::HelixParams    helixParams{};
};

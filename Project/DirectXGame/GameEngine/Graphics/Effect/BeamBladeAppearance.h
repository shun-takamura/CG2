#pragma once
#include "Vector3.h"

/// <summary>
/// ビームの刃の見た目の調整値（JSON・ImGui で触る物）。長さは付ける行列の中（ローカル）の単位。
/// 刃は根元（原点）から +Z へ伸び、幅は Y、厚みは X。断面は菱形（稜線の位置で両刃〜片刃）、先はとがる。
/// 片刃で刃が下向き：ridgeOffset を上（+）へ寄せ、backAnchor＝1（峰を真っすぐ）、tipOffset＝1（先を峰の線に）。
/// </summary>
struct BeamBladeAppearance {
	// ----- 形 -----
	float length = 1.2f;          // 根元から先までの長さ
	float halfWidth = 0.07f;      // 幅のいちばん広い所の半分
	float halfThickness = 0.012f; // 稜線の厚みの半分
	float baseWidth = 0.6f;       // 根元の幅（halfWidth に対する比）
	float tipLength = 0.3f;       // 先のとがる部分の長さ（length に対する比）
	float tipOffset = 0.0f;       // 先の位置（幅の中で。-1＝下の縁, 0＝中央, 1＝上の縁）
	float ridgeOffset = 0.0f;     // 稜線（いちばん厚い所）の位置（-1..1。0＝両刃、上へ寄せるほど上が峰・下が刃の片刃）
	float backAnchor = 0.0f;      // 根元で細くなるときの寄せ方（0＝中央へ, 1＝上の縁を真っすぐのまま下側だけ細く）
	// ----- 色・透け -----
	Vector3 coreColor{ 0.1f, 0.75f, 1.0f };  // 面の色（HDR 可）
	Vector3 edgeColor{ 0.7f, 1.0f, 1.0f };   // 刃の縁・稜線の色
	float intensity = 1.6f;
	float bodyAlpha = 0.55f;      // 面の不透明さ
	float edgeAlpha = 0.95f;      // 縁の不透明さ
	float edgeSharpness = 6.0f;   // 縁の明るい帯の細さ（大きいほど細い）
	float fresnelPower = 3.0f;    // 斜めから見た面ほど明るく（輪郭が立つ）
	float fresnelStrength = 0.6f;
	float bloomScale = 0.5f;      // 発光 RT に出す倍率
	// ----- 出し入れ（秒） -----
	float extendTime = 0.08f;
	float retractTime = 0.15f;
};

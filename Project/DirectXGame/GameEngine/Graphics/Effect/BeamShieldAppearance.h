#pragma once
#include "Vector3.h"

/// <summary>ビームシールドの見た目の調整値（JSON・ImGui で触る物）。長さはローカル（付ける行列の中）の単位</summary>
struct BeamShieldAppearance {
	// ----- 形（縦長の六角形。上下がとがる。中心＝発生装置） -----
	float halfWidth = 1.0f;        // 肩の幅の半分
	float halfHeight = 1.6f;       // 発生装置から上下の先までの長さの基準
	float topY = 1.0f;             // 上の先（halfHeight に対する比。発生装置より上）
	float shoulderY = 0.45f;       // 肩（いちばん幅の広い所）の高さ
	float waistX = 0.7f;           // 下の角の幅（halfWidth に対する比）
	float waistY = -0.55f;         // 下の角の高さ
	float bottomY = -1.1f;         // 下の先
	// ----- 縁のゆらぎ（大まかな形は変えず、縁だけ細かく変わる） -----
	float edgeNoiseAmp = 0.04f;    // 縁の出入り
	float edgeNoiseFreq = 40.0f;   // 1 周あたりのギザギザの数
	float edgeNoiseSpeed = 6.0f;   // ギザギザが入れ替わる速さ
	// ----- 集中線（発生装置から放射状の筋） -----
	float streakCount = 160.0f;    // 1 周あたりの細い筋の数（まばらな太い筋はこの 0.37 倍）
	float streakSharpness = 4.0f;  // 筋の細さ（大きいほど細い）
	float streakSpeed = 1.5f;      // 筋が入れ替わる速さ
	float streakIntensity = 0.6f;  // 筋の明るさ
	// ----- 透け具合 -----
	float coreRadius = 0.35f;      // 発生装置の近くの不透明な範囲
	float coreAlpha = 0.9f;
	float edgeWidth = 0.08f;       // 縁の不透明な帯の幅
	float edgeAlpha = 0.85f;
	float midAlpha = 0.18f;        // その間（いちばん透ける所）
	float innerScale = 0.3f;       // 発生装置を囲む小さな六角形（外形に対する比）
	float innerAlpha = 0.75f;
	// ----- 色 -----
	Vector3 color{ 0.05f, 0.5f, 1.0f };       // 本体の色（HDR 可）
	Vector3 edgeColor{ 0.35f, 0.85f, 1.0f };  // 縁と芯の色
	float intensity = 1.4f;
	float bloomScale = 0.6f;       // 発光 RT に出す倍率
	// ----- 被弾の光り -----
	float flashIntensity = 1.2f;   // 被弾の瞬間に明るさへ足す倍率
	float flashEdgeNoise = 1.0f;   // 被弾の瞬間に縁のゆらぎを何倍にするか
	float flashDecay = 6.0f;       // 光りが引く速さ [1/s]
};

#pragma once

// ============================================
// エディタのドラッグ&ドロップで運ぶペイロード定義（エンジン所有）。
// ソースはアセットブラウザ（SceneEditorWindow, ゲーム側）、ターゲットは
// ViewportWindow / 各 Instance の Inspector / EffectEditorWindow（すべてエンジン側）。
//
// 中身は POD な文字列と int だけでゲーム固有の型を含まないため、
// エンジンとゲームの共有契約としてここに集約する。
// ============================================

// テクスチャパスをそのまま運ぶ（Sprite/Object3D/Primitive/Effect の Inspector が受け取る）
#define SPRITE_DROP_PAYLOAD_TYPE "SPRITE_DROP"

// Effect Editor 用：エフェクトのコンポーネント種別だけを運ぶ
#define EFFECT_COMP_DROP_PAYLOAD_TYPE "EFFECT_COMP_DROP"

#define MODEL_DROP_PAYLOAD_TYPE     "MODEL_DROP"
#define ANIMATED_DROP_PAYLOAD_TYPE  "ANIMATED_DROP"
#define PRIMITIVE_DROP_PAYLOAD_TYPE "PRIMITIVE_DROP"
#define MATERIAL_DROP_PAYLOAD_TYPE  "MATERIAL_DROP"
#define PREFAB_DROP_PAYLOAD_TYPE    "PREFAB_DROP"
#define ANIM_DROP_PAYLOAD_TYPE      "ANIM_DROP"
// SceneEditor のエフェクト一覧から運ぶリソース名
#define EFFECT_RES_DROP_PAYLOAD_TYPE "EFFECT_RES_DROP"

// テクスチャパスをそのまま運ぶ
struct SpriteDropPayload {
    char texturePath[384];
};

// 静的モデル（dirPath + filename）
struct ModelDropPayload {
    char dirPath[256];
    char filename[128];
};

// アニメーションモデル（dirPath + filename + 拡張子）
struct AnimatedDropPayload {
    char dirPath[256];
    char filename[128];
};

// プリミティブ（PrimitiveInstance::PrimitiveType を int で運ぶ）
struct PrimitiveDropPayload {
    int primitiveType;
};

// マテリアル（.mat ファイルパス）
struct MaterialDropPayload {
    char materialPath[384];
};

// アニメーション（.anim ファイルパス）
struct AnimDropPayload {
    char animPath[384];
};

// プリファブ名
struct PrefabDropPayload {
    char prefabName[128];
};

// エフェクト名（EffectManager に登録されたエフェクトの name）
struct EffectResDropPayload {
    char effectName[128];
};

// Effect Component（種別を運ぶ。0=Primitive, 1=Particle, 2=Light, 3=Sound）
// kind==Primitive のときだけ meshType（0=Plane, 1=Box, 2=Sphere, 3=Ring,
// 4=Cylinder, 5=Helix, 6=Beam, 7=Lightning）が意味を持つ＝置いた瞬間に形状確定。
struct EffectComponentDropPayload {
    int kind;
    int meshType;
};

#pragma once

// ============================================
// エンジン側が共有するドラッグ&ドロップのペイロード定義。
// アセットブラウザ（SceneEditorWindow, ゲーム側）と、エンジン側の各エディタ
// （EffectEditorWindow / 各 Instance の Inspector）の両方が使うものをここに置く。
//
// ゲーム専用のペイロード（MODEL_DROP / PRIMITIVE_DROP / PREFAB_DROP /
// EFFECT_RES_DROP 等）は SceneEditorWindow.h に残す。
// ============================================

// テクスチャパスをそのまま運ぶ（Sprite/Object3D/Primitive/Effect の Inspector が受け取る）
#define SPRITE_DROP_PAYLOAD_TYPE "SPRITE_DROP"

// Effect Editor 用：エフェクトのコンポーネント種別だけを運ぶ
#define EFFECT_COMP_DROP_PAYLOAD_TYPE "EFFECT_COMP_DROP"

// テクスチャパスをそのまま運ぶ
struct SpriteDropPayload {
    char texturePath[384];
};

// Effect Component（種別だけ運ぶ。0=Primitive, 1=Particle, 2=Light, 3=Sound）
struct EffectComponentDropPayload {
    int kind;
};

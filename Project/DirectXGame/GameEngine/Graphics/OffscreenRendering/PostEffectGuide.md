# PostEffect マルチパスシステム 使い方ガイド

## 概要

PostEffectクラスは、画面全体にかけるポストエフェクト（ぼかし、グレースケール、ヴィネットなど）を管理するシステムです。

複数のエフェクトを自由に重ねがけでき、ON/OFF・順番入れ替え・パラメータ調整がすべてランタイムで行えます。

---

## 仕組み

### 基本の流れ

1. 3Dシーンを**RenderTexture A（裏紙A）**に描画する
2. ONになっているエフェクトを**リストの順番に**適用する
3. 最後のエフェクトの結果が**Swapchain（画面）**に描画される

### ピンポン描画（エフェクト2つ以上の場合）

エフェクトを重ねがけするとき、RenderTexture AとBを交互に使います。

```
シーン → [裏紙A]
[裏紙A] → Smoothing → [裏紙B]
[裏紙B] → Vignette → [画面]
```

エフェクトが1つだけなら中間パスは不要で、直接画面に描画します。
全エフェクトOFFなら、シーンをそのまま画面にコピーします（無駄な処理なし）。

---

## ゲームコードからの使い方

### エフェクトのON/OFF

```cpp
Game::GetPostEffect()->SetEffectEnabled("Smoothing", true);   // ON
Game::GetPostEffect()->SetEffectEnabled("Smoothing", false);  // OFF
```

### パラメータ設定

```cpp
// float型パラメータ
Game::GetPostEffect()->SetEffectParam("Vignette", "intensity", 0.8f);
Game::GetPostEffect()->SetEffectParam("Vignette", "power", 0.8f);
Game::GetPostEffect()->SetEffectParam("Vignette", "scale", 16.0f);

// int型パラメータ
Game::GetPostEffect()->SetEffectParam("Smoothing", "kernelSize", 5);
```

### 適用順序の変更

```cpp
Game::GetPostEffect()->SetEffectOrder({"Smoothing", "Grayscale", "Vignette"});
```

リストの先頭から順に適用されます。指定しなかったエフェクトは末尾に移動します。

### 全エフェクトリセット

```cpp
Game::GetPostEffect()->ResetEffects();
```

すべてのエフェクトをOFFにし、パラメータをデフォルト値に戻します。

### エフェクトの状態確認

```cpp
bool isOn = Game::GetPostEffect()->IsEffectEnabled("Vignette");
```

### ダメージ演出（ヘルパー関数）

```cpp
// damageRatio: 0.0 = 満タン, 1.0 = 瀕死
Game::GetPostEffect()->ApplyDamageEffect(0.5f);
```

内部でVignette・Sepia・Grayscaleを自動的にON/OFFしてパラメータを設定します。

---

## 登録済みエフェクト一覧

| エフェクト名 | 説明 | cbuffer | パラメータ |
|---|---|---|---|
| Grayscale | 白黒化（BT.709） | なし | なし |
| Sepia | セピア調 | あり | `intensity`(float), `sepiaColorR`(float), `sepiaColorG`(float), `sepiaColorB`(float) |
| Vignette | 周辺減光 | あり | `intensity`(float), `power`(float), `scale`(float) |
| Smoothing | ぼかし（BoxFilter） | あり | `kernelSize`(int, 奇数) |

---

## ImGuiでの操作

`PostEffect::ShowImGui()` を呼ぶと以下の操作ができます。

- **チェックボックス**: エフェクトのON/OFF
- **ドラッグ**: エフェクトの順番入れ替え（項目をドラッグして上下に移動）
- **スライダー**: ONになっているエフェクトのパラメータ調整
- **Reset All**: 全エフェクトをOFFにしてデフォルト値に戻す

---

## 新しいエフェクトの追加手順

### 手順1: シェーダーファイルを作成

`Resources/Shaders/` にピクセルシェーダーを作成します。

**cbufferありの場合:**

```hlsl
#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer MyEffectParams : register(b0)
{
    float myParam;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // ここに加工処理を書く

    return output;
}
```

**cbufferなしの場合:**

cbufferの宣言を省略するだけです。それ以外は同じです。

### 手順2: PostEffect.cpp にC++側の構造体とエフェクト登録を追加

**2-A. cbuffer構造体を定義（cbufferありの場合のみ）**

ファイル先頭の構造体定義エリアに追加します。シェーダーのcbufferとレイアウトを一致させてください。

```cpp
struct MyEffectParamsCB
{
    float myParam = 1.0f;
    float _padding[3];  // 16バイトアライメント用
};
```

> **注意**: DX12の定数バッファは16バイト単位でアライメントされます。構造体の合計サイズが16の倍数になるようにパディングを入れてください。

**2-B. CreatePipelineStates() にエフェクト登録を追加**

関数の末尾（最後のエフェクト登録の後）に追加します。

cbufferありの場合:

```cpp
// ----- MyEffect -----
{
    EffectEntry effect;
    effect.name = "MyEffect";
    effect.enabled = false;
    effect.needsCBuffer = true;
    effect.constantBufferSize = sizeof(MyEffectParamsCB);
    effect.constantBuffer = CreateConstantBuffer(sizeof(MyEffectParamsCB));
    effect.constantBuffer->Map(0, nullptr, &effect.constantBufferMappedPtr);
    effect.pipelineState = createEffectPipeline(L"Resources/Shaders/MyEffect.PS.hlsl", true);
    effect.params["myParam"] = 1.0f;
    effects_.push_back(std::move(effect));
}
```

cbufferなしの場合:

```cpp
// ----- MyEffect -----
{
    EffectEntry effect;
    effect.name = "MyEffect";
    effect.enabled = false;
    effect.needsCBuffer = false;
    effect.pipelineState = createEffectPipeline(L"Resources/Shaders/MyEffect.PS.hlsl", false);
    effects_.push_back(std::move(effect));
}
```

### 手順3: cbufferの更新・ImGui・リセットに分岐を追加

**3-A. UpdateEffectConstantBuffer()（cbufferありの場合のみ）**

```cpp
else if (effect.name == "MyEffect") {
    MyEffectParamsCB cb;
    cb.myParam = std::get<float>(effect.params["myParam"]);
    memcpy(effect.constantBufferMappedPtr, &cb, sizeof(cb));
}
```

**3-B. ShowImGui()（パラメータがある場合のみ）**

```cpp
else if (effect.name == "MyEffect") {
    float myParam = std::get<float>(effect.params["myParam"]);
    if (ImGui::SliderFloat("My Param", &myParam, 0.0f, 1.0f)) {
        effect.params["myParam"] = myParam;
    }
}
```

**3-C. ResetEffects()（パラメータがある場合のみ）**

```cpp
else if (effect.name == "MyEffect") {
    effect.params["myParam"] = 1.0f;
}
```

### 追加完了

これだけで新しいエフェクトが使えるようになります。

- ImGuiでON/OFF・パラメータ調整ができます
- 他のエフェクトとの重ねがけが自動で動きます
- ゲームコードから `SetEffectEnabled("MyEffect", true)` で呼び出せます

---

## 注意事項

### パラメータ名の間違いに注意

文字列ベースのAPIなので、パラメータ名を間違えてもコンパイルエラーになりません。存在しない名前を指定するとデバッグログに警告が出ます。

```cpp
// NG: スペルミス（ログに警告が出る）
SetEffectParam("Vignete", "intensity", 0.8f);

// OK
SetEffectParam("Vignette", "intensity", 0.8f);
```

### cbufferのアライメント

HLSLの定数バッファは16バイト単位でアライメントされます。C++側の構造体も同じレイアウトにしないとパラメータがずれます。

```cpp
// 4バイト × 1 = 4バイト → パディング12バイト必要
struct MyParamsCB
{
    float value;        // 4バイト
    float _padding[3];  // 12バイト（合計16バイト）
};

// 4バイト × 4 = 16バイト → パディング不要
struct MyParamsCB
{
    float a, b, c, d;   // 16バイトちょうど
};
```

### Smoothingのカーネルサイズ

奇数を指定してください。偶数を指定した場合は自動的に+1されます。値が大きいほどぼかしが強くなりますが、処理が重くなります。最大は15（15×15=225回サンプリング）です。

---

## ファイル構成

```
PostEffect.h          : ヘッダー（EffectEntry構造体、PostEffectクラス定義）
PostEffect.cpp        : 実装（マルチパス描画、API、ImGui）
RenderTexture.h/cpp   : オフスクリーン描画用テクスチャ

Resources/Shaders/
  PostProcess.hlsli   : 共通のVertexShaderOutput定義
  PostProcess.VS.hlsl : エフェクト共通の頂点シェーダー
  CopyImage.VS.hlsl   : コピー用の頂点シェーダー
  CopyImage.PS.hlsl   : コピー用のピクセルシェーダー（そのまま出力）
  Grayscale.PS.hlsl   : グレースケール
  Sepia.PS.hlsl       : セピア
  Vignette.PS.hlsl    : ヴィネット
  Smoothing.PS.hlsl   : ぼかし（BoxFilter）
```

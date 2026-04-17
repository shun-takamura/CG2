# PostEffect マルチパスシステム 使い方ガイド

## 概要

PostEffectクラスは、画面全体にかけるポストエフェクト（ぼかし、グレースケール、ヴィネットなど）を管理するシステムです。

各エフェクトは `BaseFilterEffect` を継承した独立したクラスとして実装され、複数のエフェクトを自由に重ねがけできます。ON/OFF・順番入れ替え・パラメータ調整がすべてランタイムで行えます。

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
Game::GetPostEffect()->grayscale->SetEnabled(true);   // ON
Game::GetPostEffect()->grayscale->SetEnabled(false);  // OFF
```

### パラメータ設定

各エフェクトの専用関数を使います。型安全で、間違えるとコンパイルエラーになります。

```cpp
// グレースケール
Game::GetPostEffect()->grayscale->SetIntensity(0.5f);

// ガウシアンぼかし
Game::GetPostEffect()->gaussian->SetKernelSize(5);
Game::GetPostEffect()->gaussian->SetSigma(2.0f);

// ヴィネット
Game::GetPostEffect()->vignette->SetIntensity(0.8f);
Game::GetPostEffect()->vignette->SetPower(0.8f);
Game::GetPostEffect()->vignette->SetScale(16.0f);

// セピア
Game::GetPostEffect()->sepia->SetIntensity(0.7f);
Game::GetPostEffect()->sepia->SetSepiaColor(1.0f, 0.691f, 0.402f);

// スムージング
Game::GetPostEffect()->smoothing->SetKernelSize(5);
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

### ダメージ演出（ヘルパー関数）

```cpp
// damageRatio: 0.0 = 満タン, 1.0 = 瀕死
Game::GetPostEffect()->ApplyDamageEffect(0.5f);
```

内部で複数エフェクトを自動的にON/OFFしてパラメータを設定します。

---

## 登録済みエフェクト一覧

| エフェクト名 | クラス | 説明 | パラメータ |
|---|---|---|---|
| Grayscale | GrayscaleEffect | 白黒化（BT.709） | `intensity`(float) |
| Gaussian | GaussianEffect | ガウシアンぼかし | `kernelSize`(int), `sigma`(float) |
| Sepia | SepiaEffect | セピア調 | `intensity`(float), `sepiaColorR/G/B`(float) |
| Vignette | VignetteEffect | 周辺減光 | `intensity`(float), `power`(float), `scale`(float) |
| Smoothing | SmoothingEffect | ぼかし（BoxFilter） | `kernelSize`(int) |

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

```hlsl
#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// パラメータが必要なら専用のcbufferを定義
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

パラメータが不要なら `cbuffer` は書かなくてOKです。

### 手順2: エフェクトクラスのヘッダーを作成

`MyEffect.h` を作成します。`BaseFilterEffect` を継承します。

```cpp
#pragma once
#include "BaseFilterEffect.h"

class MyEffect : public BaseFilterEffect
{
public:
    void Initialize(
        DirectXCore* dxCore,
        ID3D12RootSignature* copyRootSignature,
        ID3D12RootSignature* effectRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
    ) override;

    void UpdateConstantBuffer() override;
    void ShowImGui() override;
    void ResetParams() override;

    std::string GetName() const override { return "MyEffect"; }
    bool NeedsCBuffer() const override { return true; }  // cbufferなしならfalse

    // ===== 専用パラメータ設定 =====
    void SetMyParam(float value);
    float GetMyParam() const { return myParam_; }

private:
    // cbuffer構造体（シェーダーと同じレイアウト）
    struct MyEffectParamsCB
    {
        float myParam = 1.0f;   // offset: 0
        float _padding[3];      // offset: 4-15（16バイトアライメント）
    };

    float myParam_ = 1.0f;
};
```

**cbufferなしの場合**は以下がシンプルになります:

```cpp
    void UpdateConstantBuffer() override {}  // 空
    void ShowImGui() override {}             // パラメータなしなら空
    void ResetParams() override {}           // パラメータなしなら空
    bool NeedsCBuffer() const override { return false; }
    // cbuffer構造体とパラメータメンバは不要
```

### 手順3: エフェクトクラスの実装を作成

`MyEffect.cpp` を作成します。

```cpp
#include "MyEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void MyEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* copyRootSignature,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
    // シェーダーコンパイル
    IDxcBlob* psBlob = dxCore->CompileShader(
        L"Resources/Shaders/MyEffect.PS.hlsl",
        L"ps_6_0"
    );
    assert(psBlob);

    // パイプライン作成
    // cbufferありなら effectRootSignature、なしなら copyRootSignature
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
    psoDesc.pRootSignature = effectRootSignature;
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    // 定数バッファ作成（基底クラスのヘルパー）
    // cbufferなしの場合はこの2行は不要
    CreateConstantBuffer(dxCore, sizeof(MyEffectParamsCB));
    UpdateConstantBuffer();
}

void MyEffect::UpdateConstantBuffer()
{
    if (!constantBufferMappedPtr_) return;

    MyEffectParamsCB cb;
    cb.myParam = myParam_;
    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void MyEffect::ShowImGui()
{
#ifdef USE_IMGUI
    ImGui::SliderFloat("My Param##MyEffect", &myParam_, 0.0f, 1.0f);
#endif
}

void MyEffect::ResetParams()
{
    myParam_ = 1.0f;
}

void MyEffect::SetMyParam(float value)
{
    myParam_ = std::clamp(value, 0.0f, 1.0f);
}
```

### 手順4: PostEffectに登録する

**4-A. PostEffect.h を編集**

ヘッダーのインクルードとpublicメンバを追加します。

```cpp
// インクルード追加
#include "MyEffect.h"

// publicメンバ追加
MyEffect* myEffect = nullptr;
```

**4-B. PostEffect.cpp の InitializeEffects() に登録を追加**

```cpp
myEffect = static_cast<MyEffect*>(registerEffect(std::make_unique<MyEffect>()));
```

### 追加完了

これだけで新しいエフェクトが使えるようになります。

- ImGuiでON/OFF・パラメータ調整ができます
- 他のエフェクトとの重ねがけが自動で動きます
- ゲームコードから `Game::GetPostEffect()->myEffect->SetEnabled(true)` で使えます
- PostEffect.cppにelse if分岐を書く必要はありません

---

## 注意事項

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

### カーネルサイズ（SmoothingとGaussian共通）

奇数を指定してください。偶数を指定した場合は自動的に+1されます。値が大きいほどぼかしが強くなりますが、処理が重くなります。最大は15（15×15=225回サンプリング）です。

---

## ファイル構成

```
PostEffect.h/cpp               : マルチパス管理、ヘルパー関数
BaseFilterEffect.h/cpp          : エフェクト基底クラス

GrayscaleEffect.h/cpp           : グレースケール
GaussianEffect.h/cpp            : ガウシアンぼかし
SepiaEffect.h/cpp               : セピア
VignetteEffect.h/cpp            : ヴィネット
SmoothingEffect.h/cpp           : スムージング（BoxFilter）

RenderTexture.h/cpp             : オフスクリーン描画用テクスチャ

Resources/Shaders/
  PostProcess.hlsli             : 共通のVertexShaderOutput定義
  PostProcess.VS.hlsl           : エフェクト共通の頂点シェーダー
  CopyImage.VS.hlsl             : コピー用の頂点シェーダー
  CopyImage.PS.hlsl             : コピー用のピクセルシェーダー
  Grayscale.PS.hlsl             : グレースケール
  GaussianFilter.PS.hlsl        : ガウシアンぼかし
  Sepia.PS.hlsl                 : セピア
  Vignette.PS.hlsl              : ヴィネット
  Smoothing.PS.hlsl             : スムージング（BoxFilter）
```

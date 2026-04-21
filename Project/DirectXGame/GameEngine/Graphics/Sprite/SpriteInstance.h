#pragma once
#include "Vector3.h"
#include "Vector2.h"
#include "SpriteManager.h"
#include "SpriteMaterialData.h" 
#include "SpriteVertexData.h"
#include "Transform.h"
#include "TransformationMatrix.h"
#include "TextureManager.h"
#include "ConvertString.h"

#ifdef USE_IMGUI
#include "IImGuiEditable.h"
#endif

class SpriteManager;

class SpriteInstance
#ifdef USE_IMGUI
    : public IImGuiEditable
#endif
{
    std::string name_;
    std::string textureFilePath_;

    Vector2 position_ = { 0.0f, 0.0f };
    Vector2 anchorPoint_ = { 0.0f, 0.0f };

    bool isFlipX_ = false;
    bool isFlipY_ = false;

    Vector2 textureLeftTop_ = { 0.0f, 0.0f };
    Vector2 textureSize_ = { 100.0f, 100.0f };

    void AdjustTextureSize();

    float rotation_ = 0.0f;
    Vector2 size_ = { 320.0f, 180.0f };

    Transform transform{
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };

    SpriteManager* spriteManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

    // バッファ内データへのCPU側ポインタ ← 型変更
    SpriteVertexData* vertexData_ = nullptr;     // ← VertexData から変更
    uint32_t* indexData_ = nullptr;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW  indexBufferView_{};

    // マテリアル用リソース ← 型変更
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    SpriteMaterialData* materialData_ = nullptr;  // ← Material から変更

    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_ = nullptr;
    TransformationMatrix* transformationMatrixData_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE textureGpuHandle_{};
    int textureNum_ = 0;
    uint32_t textureIndex_ = 0;

    void CreateVertexBuffer();
    void CreateMaterialBuffer();
    void CreateTransformationMatrixBuffer();

public:
    SpriteInstance() = default;
#ifdef USE_IMGUI
    ~SpriteInstance() override;
#else 
    ~SpriteInstance();
#endif

#ifdef USE_IMGUI
    std::string GetName() const override { return name_; }
    std::string GetTypeName() const override { return "Sprite"; }
    void OnImGuiInspector() override;
#endif

    void Initialize(SpriteManager* spriteManager, const std::string& filePath,
        const std::string& name = "");
    void Update();
    void Draw();

    // ゲッター
    const Vector2& GetPosition() const { return position_; }
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }
    const float& GetRotation() const { return rotation_; }
    const Vector4& GetColor() const { return materialData_->color; }
    const Vector2& GetSize() const { return size_; }
    const bool& GetIsFlipX() const { return isFlipX_; }
    const bool& GetIsFlipY() const { return isFlipY_; }
    const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
    const Vector2& GetTextureSize() const { return textureSize_; }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }

    // セッター
    void SetName(const std::string& name) { name_ = name; }
    void SetPosition(const Vector2& position) { position_ = position; }
    void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }
    void SetRotation(const float& rotation) { rotation_ = rotation; }
    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetSize(const Vector2& size) { size_ = size; }
    void SetIsFlipX(const bool& isFlipX) { isFlipX_ = isFlipX; }
    void SetIsFlipY(const bool& isFlipY) { isFlipY_ = isFlipY; }
    void SetTextureLeftTop(const Vector2& textureLeftTop) { textureLeftTop_ = textureLeftTop; }
    void SetTextureSize(const Vector2& textureSize) { textureSize_ = textureSize; }
};
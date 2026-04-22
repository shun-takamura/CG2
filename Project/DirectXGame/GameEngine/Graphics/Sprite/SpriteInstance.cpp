#include "SpriteInstance.h"
#include "MathUtility.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void SpriteInstance::Initialize(SpriteManager* spriteManager, const std::string& filePath,
    const std::string& name)
{
    spriteManager_ = spriteManager;
    textureFilePath_ = filePath;

    if (name.empty()) {
        size_t pos = filePath.find_last_of("/\\");
        if (pos != std::string::npos) {
            name_ = filePath.substr(pos + 1);
        } else {
            name_ = filePath;
        }
    } else {
        name_ = name;
    }

    TextureManager::GetInstance()->LoadTexture(filePath);
    textureGpuHandle_ = TextureManager::GetInstance()->GetSrvHandleGPU(filePath);

    const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetaData(filePath);
    textureLeftTop_ = { 0.0f, 0.0f };
    textureSize_ = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };

    CreateVertexBuffer();
    CreateMaterialBuffer();
    CreateTransformationMatrixBuffer();
}

void SpriteInstance::Update()
{
    float left = 0.0f - anchorPoint_.x;
    float right = 1.0f - anchorPoint_.x;
    float top = 0.0f - anchorPoint_.y;
    float bottom = 1.0f - anchorPoint_.y;

    if (isFlipX_) {
        left = -left;
        right = -right;
    }

    if (isFlipY_) {
        top = -top;
        bottom = -bottom;
    }

    const DirectX::TexMetadata& metadata =
        TextureManager::GetInstance()->GetMetaData(textureFilePath_);
    float tex_left = textureLeftTop_.x / metadata.width;
    float tex_right = (textureLeftTop_.x + textureSize_.x) / metadata.width;
    float tex_top = textureLeftTop_.y / metadata.height;
    float tex_bottom = (textureLeftTop_.y + textureSize_.y) / metadata.height;

    // 左下
    vertexData_[0].pos = { left, bottom, 0.0f, 1.0f };           // ← position → pos
    vertexData_[0].texcoord = { tex_left, tex_bottom };
    // normal行を削除

    // 左上
    vertexData_[1].pos = { left, top, 0.0f, 1.0f };
    vertexData_[1].texcoord = { tex_left, tex_top };

    // 右下
    vertexData_[2].pos = { right, bottom, 0.0f, 1.0f };
    vertexData_[2].texcoord = { tex_right, tex_bottom };

    // 右上
    vertexData_[3].pos = { right, top, 0.0f, 1.0f };
    vertexData_[3].texcoord = { tex_right, tex_top };

    // インデックス
    indexData_[0] = 0;
    indexData_[1] = 1;
    indexData_[2] = 2;
    indexData_[3] = 1;
    indexData_[4] = 3;
    indexData_[5] = 2;

    transform.scale = { size_.x, size_.y, 1.0f };
    transform.rotate = { 0.0f, 0.0f, rotation_ };
    transform.translate = { position_.x, position_.y, 0.0f };

    Matrix4x4 worldMatrix = MakeAffineMatrix(transform);
    Matrix4x4 viewMatrix = MakeIdentity4x4();
    Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
        0.0f, 0.0f,
        WindowsApplication::kClientWidth,
        WindowsApplication::kClientHeight,
        0.0f, 100.0f
    );
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformationMatrixData_->World = worldMatrix;
    transformationMatrixData_->WVP = wvpMatrix;
}

void SpriteInstance::Draw()
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = spriteManager_->GetDxCore()->GetCommandList();

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // PS b0: Material
    commandList->SetGraphicsRootConstantBufferView(
        0,
        materialResource_->GetGPUVirtualAddress()
    );

    // VS b0: Transformation
    commandList->SetGraphicsRootConstantBufferView(
        1,
        transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // PS t0: Texture
    commandList->SetGraphicsRootDescriptorTable(
        2,
        TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

SpriteInstance::~SpriteInstance()
{
    if (vertexResource_ && vertexData_) {
        vertexResource_->Unmap(0, nullptr);
        vertexData_ = nullptr;
    }

    if (indexResource_ && indexData_) {
        indexResource_->Unmap(0, nullptr);
        indexData_ = nullptr;
    }

    if (materialResource_ && materialData_) {
        materialResource_->Unmap(0, nullptr);
        materialData_ = nullptr;
    }

    if (transformationMatrixResource_ && transformationMatrixData_) {
        transformationMatrixResource_->Unmap(0, nullptr);
        transformationMatrixData_ = nullptr;
    }
}

void SpriteInstance::AdjustTextureSize()
{
    const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetaData(textureFilePath_);

    textureSize_.x = static_cast<float>(metadata.width);
    textureSize_.y = static_cast<float>(metadata.height);

    size_ = textureSize_;
}

void SpriteInstance::CreateVertexBuffer()
{
    // SpriteVertexData を使用
    vertexResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(SpriteVertexData) * 4);

    indexResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(uint32_t) * 6);

    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

    // 座標セット (position → pos)
    vertexData_[0].pos = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexData_[1].pos = { static_cast<float>(size_.x), 0.0f, 0.0f, 1.0f };
    vertexData_[2].pos = { 0.0f, static_cast<float>(size_.y), 0.0f, 1.0f };
    vertexData_[3].pos = { static_cast<float>(size_.x), static_cast<float>(size_.y), 0.0f, 1.0f };

    // UV
    vertexData_[0].texcoord = { 0.0f, 0.0f };
    vertexData_[1].texcoord = { 1.0f, 0.0f };
    vertexData_[2].texcoord = { 0.0f, 1.0f };
    vertexData_[3].texcoord = { 1.0f, 1.0f };

    // normalの設定は削除（SpriteVertexDataには存在しない）

    // バッファビュー設定
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(SpriteVertexData) * 4;
    vertexBufferView_.StrideInBytes = sizeof(SpriteVertexData);

    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void SpriteInstance::CreateMaterialBuffer()
{
    // SpriteMaterialData を使用
    materialResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(SpriteMaterialData));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = { 1, 1, 1, 1 };
    // enableLighting の設定は削除（SpriteMaterialDataには存在しない）
    materialData_->uvTransform = MakeIdentity4x4();
}

void SpriteInstance::CreateTransformationMatrixBuffer()
{
    transformationMatrixResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(TransformationMatrix));
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WVP = MakeIdentity4x4();
}

#ifdef USE_IMGUI

void SpriteInstance::OnImGuiInspector()
{
    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat2("Position", &position_.x, 1.0f);
        ImGui::DragFloat2("Size", &size_.x, 1.0f);

        float rotationDegree = rotation_ * (180.0f / 3.14159265f);
        if (ImGui::DragFloat("Rotation", &rotationDegree, 1.0f)) {
            rotation_ = rotationDegree * (3.14159265f / 180.0f);
        }

        ImGui::DragFloat2("Anchor Point", &anchorPoint_.x, 0.01f, 0.0f, 1.0f);
    }

    // UV座標
    if (ImGui::CollapsingHeader("UV Coordinates", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat2("Texture Left Top", &textureLeftTop_.x, 1.0f);
        ImGui::DragFloat2("Texture Size", &textureSize_.x, 1.0f);

        if (ImGui::Button("Reset to Texture Size")) {
            AdjustTextureSize();
        }
    }

    // Flip
    if (ImGui::CollapsingHeader("Flip")) {
        ImGui::Checkbox("Flip X", &isFlipX_);
        ImGui::Checkbox("Flip Y", &isFlipY_);
    }

    // Color
    if (ImGui::CollapsingHeader("Color")) {
        ImGui::ColorEdit4("Color", &materialData_->color.x);
    }

    // Texture Info
    if (ImGui::CollapsingHeader("Texture")) {
        ImGui::Text("File: %s", textureFilePath_.c_str());

        const DirectX::TexMetadata& metadata =
            TextureManager::GetInstance()->GetMetaData(textureFilePath_);
        ImGui::Text("Original Size: %dx%d",
            static_cast<int>(metadata.width),
            static_cast<int>(metadata.height));
    }
}
#endif
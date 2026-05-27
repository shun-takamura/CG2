#include "EffectPrimitiveRenderer.h"
#include "PrimitiveGenerator.h"
#include "TextureManager.h"

namespace {
    // primitiveType（int）から MeshData を生成。EffectDef.h のmeshType値と互換。
    // 0=Plane, 1=Box, 2=Sphere, 3=Ring, 4=Cylinder, 5=Helix
    // Ring/Cylinder/Helix は渡された params でジオメトリを生成する。
    MeshData GenerateByType(int type,
                            const PrimitiveGenerator::RingParams& ring,
                            const PrimitiveGenerator::CylinderParams& cyl,
                            const PrimitiveGenerator::HelixParams& helix) {
        switch (type) {
        case 0: return PrimitiveGenerator::CreatePlane();
        case 1: return PrimitiveGenerator::CreateBox();
        case 2: return PrimitiveGenerator::CreateSphere();
        case 3: return PrimitiveGenerator::CreateRing(ring);
        case 4: return PrimitiveGenerator::CreateCylinder(cyl);
        case 5: return PrimitiveGenerator::CreateHelix(helix);
        default: return PrimitiveGenerator::CreatePlane();
        }
    }
}

void EffectPrimitiveRenderer::Initialize(int primitiveType, const std::string& texturePath,
                                         const PrimitiveGenerator::RingParams& ringParams,
                                         const PrimitiveGenerator::CylinderParams& cylinderParams,
                                         const PrimitiveGenerator::HelixParams& helixParams) {
    primitiveType_ = primitiveType;
    MeshData md = GenerateByType(primitiveType, ringParams, cylinderParams, helixParams);
    mesh_.Initialize(md);

    // エフェクト用既定：加算ブレンド・深度書き込みなし・背面カリング無効
    mesh_.SetBlendMode(PrimitivePipeline::kBlendModeAdd);
    mesh_.SetDepthWrite(false);
    mesh_.SetCullBackface(false);

    // テクスチャ
    const std::string path = texturePath.empty()
        ? std::string("Resources/Textures/white1x1.dds")
        : texturePath;
    mesh_.SetTexture(path);
}

void EffectPrimitiveRenderer::Update(Camera* camera, float deltaTime) {
    mesh_.Update(camera, deltaTime);
}

void EffectPrimitiveRenderer::Draw() {
    mesh_.Draw();
}

void EffectPrimitiveRenderer::UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos) {
    mesh_.UpdatePreviewWVP(viewMatrix, viewProjectionMatrix, cameraPos);
}

void EffectPrimitiveRenderer::DrawPreview() {
    mesh_.DrawPreview();
}

void EffectPrimitiveRenderer::SetDistortionTexture(const std::string& path) {
    if (path.empty()) {
        hasDistortionTexture_ = false;
        return;
    }
    TextureManager::GetInstance()->LoadTexture(path);
    distortionTextureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(path);
    hasDistortionTexture_ = true;
}

void EffectPrimitiveRenderer::DrawDistortionPass() {
    if (!hasDistortionTexture_) return;
    mesh_.DrawDistortionPass(distortionTextureSrvIndex_);
}

void EffectPrimitiveRenderer::DrawDistortionPassPreview() {
    if (!hasDistortionTexture_) return;
    mesh_.DrawDistortionPassPreview(distortionTextureSrvIndex_);
}

#include "EffectPrimitiveRenderer.h"
#include "PrimitiveGenerator.h"
#include "TextureManager.h"

namespace {
    /// <summary>
    /// primitiveType（EffectMeshType の値）から MeshData を生成する
    /// </summary>
    /// <param name="type">形状の番号（範囲外は Plane）</param>
    /// <param name="ring">Ring の形状パラメータ</param>
    /// <param name="cyl">Cylinder の形状パラメータ</param>
    /// <param name="helix">Helix の形状パラメータ</param>
    /// <param name="beam">Beam の形状パラメータ</param>
    /// <param name="lightning">Lightning の形状パラメータ</param>
    /// <param name="frame">Frame の形状パラメータ</param>
    /// <returns>生成したメッシュ</returns>
    MeshData GenerateByType(int type,
                            const PrimitiveGenerator::RingParams& ring,
                            const PrimitiveGenerator::CylinderParams& cyl,
                            const PrimitiveGenerator::HelixParams& helix,
                            const PrimitiveGenerator::BeamParams& beam,
                            const PrimitiveGenerator::LightningBoltParams& lightning,
                            const PrimitiveGenerator::FrameParams& frame) {
        switch (type) {
        case EffectMeshType::Plane: return PrimitiveGenerator::CreatePlane();
        case EffectMeshType::Box: return PrimitiveGenerator::CreateBox();
        case EffectMeshType::Sphere: return PrimitiveGenerator::CreateSphere();
        case EffectMeshType::Ring: return PrimitiveGenerator::CreateRing(ring);
        case EffectMeshType::Cylinder: return PrimitiveGenerator::CreateCylinder(cyl);
        case EffectMeshType::Helix: return PrimitiveGenerator::CreateHelix(helix);
        case EffectMeshType::Beam: return PrimitiveGenerator::CreateBeam(beam);
        case EffectMeshType::Lightning: return PrimitiveGenerator::CreateLightningBolt(lightning);
        case EffectMeshType::Hemisphere: return PrimitiveGenerator::CreateHemisphere();
        case EffectMeshType::Frame: return PrimitiveGenerator::CreateFrame(frame);
        default: return PrimitiveGenerator::CreatePlane();
        }
    }
}

void EffectPrimitiveRenderer::Initialize(int primitiveType, const std::string& texturePath,
                                         const PrimitiveGenerator::RingParams& ringParams,
                                         const PrimitiveGenerator::CylinderParams& cylinderParams,
                                         const PrimitiveGenerator::HelixParams& helixParams,
                                         const PrimitiveGenerator::BeamParams& beamParams,
                                         const PrimitiveGenerator::LightningBoltParams& lightningParams,
                                         const PrimitiveGenerator::FrameParams& frameParams) {
    primitiveType_ = primitiveType;
    // パラメータを持たない形（Plane/Box/Sphere/Hemisphere）は PrimitiveInstance と同じ共有バッファを使う
    // （弾のエフェクトを出すたびに同じ形を作り直さない）
    const char* sharedKey = nullptr;
    switch (primitiveType) {
    case EffectMeshType::Plane: sharedKey = "Plane"; break;
    case EffectMeshType::Box: sharedKey = "Box"; break;
    case EffectMeshType::Sphere: sharedKey = "Sphere"; break;
    case EffectMeshType::Hemisphere: sharedKey = "Hemisphere"; break;
    default: break;
    }
    auto make = [&]() {
        return GenerateByType(primitiveType, ringParams, cylinderParams, helixParams, beamParams, lightningParams, frameParams);
    };
    if (sharedKey) {
        mesh_.InitializeShared(sharedKey, make);
    } else {
        mesh_.Initialize(make());
    }

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

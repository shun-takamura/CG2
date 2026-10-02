#include "WaterReflection.h"

#include "DirectXCore.h"
#include "SRVManager.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Camera.h"
#include "CameraForGPU.h"
#include "RenderTexture.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "WindowsApplication.h"
#include "PepperMacros.h"
#include <algorithm>
#include <cassert>

#ifdef _DEBUG
#include "imgui.h"
#endif

WaterReflection::WaterReflection() = default;
WaterReflection::~WaterReflection() = default;

void WaterReflection::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	float resolutionScale)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	object3DManager_ = object3DManager;

	const float scale = std::clamp(resolutionScale, 0.25f, 1.0f);
	const uint32_t width = static_cast<uint32_t>(WindowsApplication::kClientWidth * scale);
	const uint32_t height = static_cast<uint32_t>(WindowsApplication::kClientHeight * scale);

	// α=0 のクリアが「何も映っていない（空を映す）」の印になる
	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	renderTexture_ = std::make_unique<RenderTexture>();
	renderTexture_->Initialize(dxCore_, srvManager_, width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor);

	CreateDepthBuffer(width, height);

	cameraResource_ = dxCore_->CreateBufferResource(kCameraCBOffset * 2);
	uint8_t* mapped = nullptr;
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	viewProjData_ = reinterpret_cast<ReflectionViewProjForGPU*>(mapped);
	cameraData_ = reinterpret_cast<CameraForGPU*>(mapped + kCameraCBOffset);
	viewProjData_->viewProj = MakeIdentity4x4();
	viewProjData_->clipPlane = { 0.0f, 1.0f, 0.0f, 0.0f };
	*cameraData_ = CameraForGPU{};
}

void WaterReflection::CreateDepthBuffer(uint32_t width, uint32_t height)
{
	ID3D12Device* device = dxCore_->GetDevice();

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap_));
	assert(SUCCEEDED(hr));

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Object3D の PSO と同じ DSV フォーマット
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;

	D3D12_HEAP_PROPERTIES heapProp{};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	hr = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthBuffer_));
	assert(SUCCEEDED(hr));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthBuffer_.Get(), &dsvDesc, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void WaterReflection::AddTarget(Object3DInstance* target)
{
	if (!target) return;
	if (std::find(targets_.begin(), targets_.end(), target) != targets_.end()) return;
	targets_.push_back(target);
}

void WaterReflection::RemoveTarget(Object3DInstance* target)
{
	targets_.erase(std::remove(targets_.begin(), targets_.end(), target), targets_.end());
}

void WaterReflection::ClearTargets()
{
	targets_.clear();
}

void WaterReflection::Render(const Camera& camera)
{
	Render(camera.GetViewProjectionMatrix(), camera.GetTranslate());
}

void WaterReflection::Render(const Matrix4x4& viewProjection, const Vector3& eyePosition)
{
	PEPPER_SCOPE("WaterReflection::Render");

	// パスを飛ばすフレームでも、水面の空・フレネルはこの視点で計算するので先に保持する
	sourceViewProjection_ = viewProjection;
	sourceEyePosition_ = eyePosition;
	const Vector3& camPos = eyePosition;

	// 描画距離で絞る。遠い物は映さない（そこは空の反射が代わりに映る）
	visibleTargets_.clear();
	for (Object3DInstance* t : targets_) {
		const Vector3& p = t->GetTranslate();
		if (Length(Vector3{ p.x - camPos.x, p.y - camPos.y, p.z - camPos.z }) <= farClip_) {
			visibleTargets_.push_back(t);
		}
	}
	drawnCount_ = static_cast<uint32_t>(visibleTargets_.size());

	// 映す物が無く、RT も既に空なら何もしない（RT は SRV 状態のまま使い回す）
	if (visibleTargets_.empty() && !hasContent_ && srvReady_) {
		return;
	}

	auto* cmd = dxCore_->GetCommandList();
	PEPPER_GPU_SCOPE(cmd, "WaterReflection::Render");

	// 鏡像行列（y = h の平面で反転。行ベクトル規約なので平行移動は 4 行目）
	Matrix4x4 mirror = MakeIdentity4x4();
	mirror.m[1][1] = -1.0f;
	mirror.m[3][1] = 2.0f * waterHeight_;

	viewProjData_->viewProj = Multiply(mirror, viewProjection);
	// dot(worldPos, plane) = y - (h - offset) >= 0 の部分だけ残す
	viewProjData_->clipPlane = { 0.0f, 1.0f, 0.0f, -(waterHeight_ - clipOffset_) };
	// ライティングは実物の位置で計算するので、視点を鏡像カメラ位置にする
	cameraData_->worldPosition = { camPos.x, 2.0f * waterHeight_ - camPos.y, camPos.z };

	D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	renderTexture_->BeginRender(cmd, &dsv);
	cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	if (!visibleTargets_.empty()) {
		object3DManager_->DrawSetting();
		LightManager::GetInstance()->BindLights(cmd);

		const D3D12_GPU_VIRTUAL_ADDRESS base = cameraResource_->GetGPUVirtualAddress();
		for (Object3DInstance* t : visibleTargets_) {
			t->DrawReflection(dxCore_, base, base + kCameraCBOffset);
		}
	}

	renderTexture_->EndRender(cmd);
	srvReady_ = true;
	hasContent_ = !visibleTargets_.empty();

	// シーン RT は本体解像度なので、ビューポートを戻して返す
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(WindowsApplication::kClientWidth);
	viewport.Height = static_cast<float>(WindowsApplication::kClientHeight);
	viewport.MaxDepth = 1.0f;
	cmd->RSSetViewports(1, &viewport);
	D3D12_RECT scissor{ 0, 0,
		static_cast<LONG>(WindowsApplication::kClientWidth),
		static_cast<LONG>(WindowsApplication::kClientHeight) };
	cmd->RSSetScissorRects(1, &scissor);
}

D3D12_GPU_DESCRIPTOR_HANDLE WaterReflection::GetSrvHandle() const
{
	return srvManager_->GetGPUDescriptorHandle(renderTexture_->GetSRVIndex());
}

void WaterReflection::OnImGui()
{
#ifdef _DEBUG
	ImGui::DragFloat("Reflection Far Clip", &farClip_, 1.0f, 1.0f, 5000.0f);
	ImGui::DragFloat("Reflection Clip Offset", &clipOffset_, 0.001f, -0.5f, 0.5f);
	ImGui::Text("Reflection targets: %u / %u", drawnCount_, static_cast<uint32_t>(targets_.size()));
#endif
}

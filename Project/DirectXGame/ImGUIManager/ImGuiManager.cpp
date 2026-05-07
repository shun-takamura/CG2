#include "ImGuiManager.h"
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include "FPSWindow.h"
#include "LogWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "ViewportWindow.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include "Camera.h"
#include "WindowsApplication.h"

// CallbackWindow 経由で呼ぶデバッグUI群
#include "Game.h"
#include "LightManager.h"
#include "PostEffect.h"
#include "GPUParticleManager.h"
#include "TransitionManager.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"

#include <dxgi.h>  // DXGI_FORMAT用

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <algorithm>

ImGuiManager& ImGuiManager::Instance() {
    static ImGuiManager instance;
    return instance;
}

void ImGuiManager::Initialize(HWND hwnd, DirectXCore* dxCore, SRVManager* srvManager) {
#ifdef _DEBUG

    dxCore_ = dxCore;
    srvManager_ = srvManager;

    // ImGui用のSRVインデックスを確保
    srvIndex_ = srvManager_->Allocate();

    // ImGuiの初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Dockingを有効化
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Win32初期化
    ImGui_ImplWin32_Init(hwnd);

    // DirectX12初期化
    // 注意: RTVはSRGBフォーマットで作成されているため、SRGB版を指定
    ImGui_ImplDX12_Init(
        dxCore_->GetDevice(),
        dxCore_->GetBackBufferCount(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  // RTVのフォーマットに合わせる
        nullptr,  // SRVヒープはSRVManagerが管理
        srvManager_->GetCPUDescriptorHandle(srvIndex_),
        srvManager_->GetGPUDescriptorHandle(srvIndex_)
    );

    // フォントアトラスを明示的にビルド（docking版のレガシーAPI対応）
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // 各ウィンドウを生成
    windows_.push_back(std::make_unique<ViewportWindow>(srvManager_));
    windows_.push_back(std::make_unique<FPSWindow>());
    windows_.push_back(std::make_unique<LogWindow>());
    windows_.push_back(std::make_unique<HierarchyWindow>(this));
    windows_.push_back(std::make_unique<InspectorWindow>(this));

    // デバッグUI群を CallbackWindow 経由で登録
    windows_.push_back(std::make_unique<CallbackWindow>("Camera",
        [this]() { if (camera_) camera_->OnImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("Light",
        []() { LightManager::GetInstance()->OnImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("PostEffect",
        []() { if (auto* p = Game::GetPostEffect()) p->ShowImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("Particle",
        [this]() {
            if (gpuParticleManager_) {
                gpuParticleManager_->OnImGui();
            } else {
                ImGui::TextDisabled("GPUParticleManager is not active in the current scene.");
            }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("Transition",
        []() { TransitionManager::GetInstance()->OnImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("WebCam Devices",
        []() { CameraCapture::GetInstance()->LogDevicesToImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("QR Code",
        []() { QRCodeReader::GetInstance()->OnImGui(); }));

    // ViewportWindowをメンバに保存（後で参照するため）
    viewportWindow_ = dynamic_cast<ViewportWindow*>(windows_[0].get());

    isInitialized_ = true;

#endif // !_DEBUG
}

void ImGuiManager::Shutdown() {
#ifdef _DEBUG
    if (!isInitialized_) return;

    windows_.clear();
    editables_.clear();
    selectedObject_ = nullptr;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    isInitialized_ = false;

#endif // !_DEBUG
}

void ImGuiManager::BeginFrame() {
#ifdef _DEBUG

    if (!isInitialized_) return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif // !_DEBUG
}

void ImGuiManager::EndFrame() {
#ifdef _DEBUG

    if (!isInitialized_) return;

    // DockSpaceの設定
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);

    // メニューバー描画
    DrawMenuBar();

    // 全ウィンドウ描画
    for (auto& window : windows_) {
        window->Draw();
    }

    // ギズモ描画（選択オブジェクトがあり、ビューポートが有効な場合）
    if (selectedObject_ && viewportWindow_ && viewportWindow_->IsOpen()) {
        ImVec2 imgPos = viewportWindow_->GetImageScreenPos();
        ImVec2 imgSize = viewportWindow_->GetImageScreenSize();
        if (imgSize.x > 0 && imgSize.y > 0) {
            // 2D（Sprite）ギズモが優先（GetEditable2DPosition がnullptrでなければ）
            if (selectedObject_->GetEditable2DPosition()) {
                spriteGizmo_.Draw(selectedObject_, imgPos, imgSize,
                    (float)WindowsApplication::kClientWidth,
                    (float)WindowsApplication::kClientHeight);
            }
            // 3Dギズモ（Translate がnullptrでなければ、かつカメラがあれば）
            else if (selectedObject_->GetEditableTranslate() && camera_) {
                gizmo_.Draw(selectedObject_, camera_, imgPos, imgSize);
            }
        }
    }

    // ImGui描画
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCore_->GetCommandList());

#endif // _DEBUG
}

void ImGuiManager::Register(IImGuiEditable* editable) {
#ifdef _DEBUG

    if (!editable) return;

    // 重複チェック
    auto it = std::find(editables_.begin(), editables_.end(), editable);
    if (it == editables_.end()) {
        editables_.push_back(editable);
    }
#endif // !_DEBUG
}

void ImGuiManager::Unregister(IImGuiEditable* editable) {
#ifdef _DEBUG

    if (!editable) return;

    // 選択中のオブジェクトなら選択解除
    if (selectedObject_ == editable) {
        selectedObject_ = nullptr;
    }

    // リストから削除
    auto it = std::find(editables_.begin(), editables_.end(), editable);
    if (it != editables_.end()) {
        editables_.erase(it);
    }
#endif // !_DEBUG
}

void ImGuiManager::DrawMenuBar() {
#ifdef _DEBUG

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("ImGuiMenu")) {
            // 各ウィンドウの表示/非表示トグル
            for (auto& window : windows_) {
                bool isOpen = window->IsOpen();
                if (ImGui::MenuItem(window->GetName().c_str(), nullptr, &isOpen)) {
                    window->SetOpen(isOpen);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

#endif // DEBUG
}

void ImGuiManager::SetViewportRenderTexture(RenderTexture* renderTexture) {
#ifdef _DEBUG
    if (viewportWindow_) {
        viewportWindow_->SetRenderTexture(renderTexture);
    }
#endif // _DEBUG
}
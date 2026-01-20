#include "ImGuiManager.h"
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include "FPSWindow.h"
#include "LogWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "DirectXCore.h"
#include "SRVManager.h"

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
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    // ImGui用のSRVインデックスを確保
    srvIndex_ = srvManager_->Allocate();

    // ImGuiの初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

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

    // 各ウィンドウを生成
    windows_.push_back(std::make_unique<FPSWindow>());
    windows_.push_back(std::make_unique<LogWindow>());
    windows_.push_back(std::make_unique<HierarchyWindow>(this));
    windows_.push_back(std::make_unique<InspectorWindow>(this));

    isInitialized_ = true;
}

void ImGuiManager::Shutdown() {
    if (!isInitialized_) return;

    windows_.clear();
    editables_.clear();
    selectedObject_ = nullptr;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    isInitialized_ = false;
}

void ImGuiManager::BeginFrame() {
    if (!isInitialized_) return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::EndFrame() {
    if (!isInitialized_) return;

    // メニューバー描画
    DrawMenuBar();

    // 全ウィンドウ描画
    for (auto& window : windows_) {
        window->Draw();
    }

    // ImGui描画
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCore_->GetCommandList());
}

void ImGuiManager::Register(IImGuiEditable* editable) {
    if (!editable) return;

    // 重複チェック
    auto it = std::find(editables_.begin(), editables_.end(), editable);
    if (it == editables_.end()) {
        editables_.push_back(editable);
    }
}

void ImGuiManager::Unregister(IImGuiEditable* editable) {
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
}

void ImGuiManager::DrawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
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
}
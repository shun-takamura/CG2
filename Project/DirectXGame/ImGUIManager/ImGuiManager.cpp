#include "ImGuiManager.h"
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include "AssetLocator.h"
#include "FPSWindow.h"
#include "LogWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "ViewportWindow.h"
#include "SceneEditorWindow.h"
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
#include "Effect/EffectManager.h"
#include "Effect/EffectEditorWindow.h"
#include "EffectHierarchyWindow.h"
#include "TransitionManager.h"
#include "DebugCamera.h"
#include "Vector3.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Components/CollisionManager.h"
#include "TimeGroup.h"

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
        [this]() {
            // ===== Debug Camera toggle =====
            BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
            if (scene) {
                bool useDebug = scene->GetUseDebugCamera();
                if (ImGui::Checkbox("Use Debug Camera", &useDebug)) {
                    scene->SetUseDebugCamera(useDebug);
                }
                if (useDebug) {
                    if (DebugCamera* dc = scene->GetDebugCamera()) {
                        ImGui::TextDisabled("L-Drag: Orbit  /  M-Drag: Pan  /  Wheel: Zoom");

                        // ピボット
                        Vector3 pivot = { 0.0f, 0.0f, 0.0f };
                        // DebugCamera にピボットgetterが無いので、UIで編集する値を別管理。
                        // 単純化：スライダーで毎フレームSetする（値の保持は ImGui の static で）
                        static float pivotV[3] = { 0.0f, 0.0f, 0.0f };
                        if (ImGui::DragFloat3("Pivot", pivotV, 0.1f)) {
                            dc->SetPivot({ pivotV[0], pivotV[1], pivotV[2] });
                        }

                        static float distance = 10.0f;
                        if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 500.0f)) {
                            dc->SetDistance(distance);
                        }

                        static float fovYDeg = 45.0f;
                        if (ImGui::DragFloat("Fov Y (deg)", &fovYDeg, 0.5f, 10.0f, 120.0f)) {
                            dc->SetFovY(fovYDeg * 3.14159265f / 180.0f);
                        }
                    }
                }
                ImGui::Separator();
            }
            // ===== Scene camera info =====
            if (camera_) camera_->OnImGui();
        }));
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

    // Effect Editor（プレビューRT付き）
    auto effectEditor = std::make_unique<EffectEditorWindow>(dxCore_, srvManager_);
    effectEditor->Initialize();
    effectEditorWindow_ = effectEditor.get();
    windows_.push_back(std::move(effectEditor));

    // Effect Hierarchy（編集中エフェクトのコンポーネントを種類別表示）
    windows_.push_back(std::make_unique<EffectHierarchyWindow>(this, effectEditorWindow_));
    windows_.push_back(std::make_unique<CallbackWindow>("Collision",
        []() {
            auto* cm = CollisionManager::GetInstance();
            bool drawDebug = cm->IsDrawDebugEnabled();
            if (ImGui::Checkbox("Draw Colliders", &drawDebug)) {
                cm->SetDrawDebugEnabled(drawDebug);
            }
            ImGui::TextDisabled("- Tag-colored when not colliding");
            ImGui::TextDisabled("- Red when colliding this frame");
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("Scene Timeline",
        []() {
            auto* sm = SceneManager::GetInstance();
            BaseScene* scene = sm ? sm->GetCurrentScene() : nullptr;
            const std::string& name = sm ? sm->GetCurrentSceneName() : std::string{};

            // 現在のシーン名
            ImGui::Text("Current Scene:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s",
                name.empty() ? "(none)" : name.c_str());

            ImGui::Separator();

            if (!scene) {
                ImGui::TextDisabled("No active scene.");
                return;
            }

            // 経過秒の表示
            float elapsed = scene->GetElapsedSeconds();
            ImGui::Text("Elapsed: %.2f sec", elapsed);

            // シーク用の上限（必要に応じてシーン側から取得する仕組みに置き換え予定）
            static float seekMax = 600.0f; // デフォルト10分
            ImGui::SliderFloat("Seek Max (sec)", &seekMax, 10.0f, 1800.0f, "%.0f");

            // シーク値（毎フレーム経過秒で初期化）
            float seekValue = elapsed;
            if (ImGui::SliderFloat("Seek", &seekValue, 0.0f, seekMax, "%.2f sec")) {
                scene->Seek(seekValue);
            }

            // 細かい移動ボタン
            if (ImGui::Button("-1s")) { scene->Seek(elapsed - 1.0f); }
            ImGui::SameLine();
            if (ImGui::Button("-0.1s")) { scene->Seek(elapsed - 0.1f); }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { scene->Seek(0.0f); }
            ImGui::SameLine();
            if (ImGui::Button("+0.1s")) { scene->Seek(elapsed + 0.1f); }
            ImGui::SameLine();
            if (ImGui::Button("+1s")) { scene->Seek(elapsed + 1.0f); }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("TimeControl",
        [this]() {
            // --- グローバル ---
            ImGui::TextUnformatted("Global");
            ImGui::Separator();
            float globalScale = dxCore_ ? dxCore_->GetTimeScale() : 1.0f;
            if (ImGui::SliderFloat("Global TimeScale", &globalScale, 0.0f, 4.0f, "%.2f")) {
                if (dxCore_) dxCore_->SetTimeScale(globalScale);
            }
            if (ImGui::Button("Pause")) { if (dxCore_) dxCore_->SetTimeScale(0.0f); }
            ImGui::SameLine();
            if (ImGui::Button("0.25x")) { if (dxCore_) dxCore_->SetTimeScale(0.25f); }
            ImGui::SameLine();
            if (ImGui::Button("0.5x"))  { if (dxCore_) dxCore_->SetTimeScale(0.5f); }
            ImGui::SameLine();
            if (ImGui::Button("1x"))    { if (dxCore_) dxCore_->SetTimeScale(1.0f); }
            ImGui::SameLine();
            if (ImGui::Button("2x"))    { if (dxCore_) dxCore_->SetTimeScale(2.0f); }

            ImGui::Spacing();

            // --- 現在シーン ---
            BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
            ImGui::TextUnformatted("Current Scene");
            ImGui::Separator();
            if (scene) {
                // ----- グループ別 TimeScale -----
                ImGui::TextUnformatted("Time Groups");
                for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i) {
                    TimeGroup g = static_cast<TimeGroup>(i);
                    float s = scene->GetTimeScale(g);
                    char label[32];
                    std::snprintf(label, sizeof(label), "%s##ts", GetTimeGroupName(g));
                    if (ImGui::SliderFloat(label, &s, 0.0f, 4.0f, "%.2f")) {
                        scene->SetTimeScale(g, s);
                    }
                }
                ImGui::Spacing();

                // ----- プリセット -----
                if (ImGui::Button("All Pause")) {
                    for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i)
                        scene->SetTimeScale(static_cast<TimeGroup>(i), 0.0f);
                }
                ImGui::SameLine();
                if (ImGui::Button("All Play")) {
                    for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i)
                        scene->SetTimeScale(static_cast<TimeGroup>(i), 1.0f);
                }
                ImGui::SameLine();
                if (ImGui::Button("HitStop")) {
                    scene->SetTimeScale(TimeGroup::World,  0.05f);
                    scene->SetTimeScale(TimeGroup::Player, 0.05f);
                    scene->SetTimeScale(TimeGroup::UI,     1.0f);
                }
                if (ImGui::Button("Just Dodge")) {
                    scene->SetTimeScale(TimeGroup::World,  0.3f);
                    scene->SetTimeScale(TimeGroup::Player, 1.0f);
                    scene->SetTimeScale(TimeGroup::UI,     1.0f);
                }
                ImGui::SameLine();
                if (ImGui::Button("BotW Rush")) {
                    scene->SetTimeScale(TimeGroup::World,  0.2f);
                    scene->SetTimeScale(TimeGroup::Player, 1.5f);
                    scene->SetTimeScale(TimeGroup::UI,     1.0f);
                }

                ImGui::Spacing();
                ImGui::Text("dt World=%.4f Player=%.4f UI=%.4f",
                    scene->GetScaledDeltaTime(TimeGroup::World),
                    scene->GetScaledDeltaTime(TimeGroup::Player),
                    scene->GetScaledDeltaTime(TimeGroup::UI));
            } else {
                ImGui::TextDisabled("No active scene.");
            }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("WebCam Devices",
        []() { CameraCapture::GetInstance()->LogDevicesToImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("QR Code",
        []() { QRCodeReader::GetInstance()->OnImGui(); }));

    // シーンエディタ（モデル一覧の非同期スキャン + 動的オブジェクト追加・削除）
    windows_.push_back(std::make_unique<SceneEditorWindow>(this));

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

        // Assets メニュー: ロード経路（FS / Pack）の確認・切り替え
        if (ImGui::BeginMenu("Assets")) {
            auto* loc = AssetLocator::GetInstance();
            ImGui::Text("Mode: %s", loc->GetModeName());
            ImGui::Separator();
            ImGui::TextDisabled("Switch (no auto reload):");
            if (ImGui::MenuItem("Filesystem", nullptr, !loc->IsPackMode())) {
                loc->InitializeFromFilesystem();
            }
            if (ImGui::MenuItem("Pack (Generated/Assets.pack)", nullptr, loc->IsPackMode())) {
                // 開発時/配布時の両候補を試す
                if (!loc->InitializeFromPack("../Generated/Assets.pack") &&
                    !loc->InitializeFromPack("Generated/Assets.pack")) {
                    // どちらも失敗したら FS に戻す
                    loc->InitializeFromFilesystem();
                }
            }
            ImGui::Separator();
            ImGui::TextDisabled("Note: existing loaded assets are NOT reloaded.");
            ImGui::TextDisabled("Restart with --use-pack / --use-fs for clean test.");
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
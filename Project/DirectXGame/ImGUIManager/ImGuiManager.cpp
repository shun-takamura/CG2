#include "ImGuiManager.h"
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include "AssetLocator.h"
#include "FPSWindow.h"
#include "PepperWindow.h"
#include "LogWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "SceneEditorWindow.h"
#include "ViewportWindow.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include "Camera.h"
#include "WindowsApplication.h"

// CallbackWindow 経由で呼ぶデバッグUI群（すべてエンジン機能。
// ゲーム固有のパネルはホスト側が AddCallbackWindow で足す）
#include "Framework.h"
#include "LightManager.h"
#include "PostEffect.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "Effect/EffectEditorWindow.h"
#include "EffectHierarchyWindow.h"
#include "EffectPaletteWindow.h"
#include "DebugCamera.h"
#include "Vector3.h"
#include "MathUtility.h"
#include "Scene.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "TimeGroup.h"
#include "Physics/CollisionSystem.h"

#include <dxgi.h>  // DXGI_FORMAT用

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <algorithm>

EditorHostHooks ImGuiManager::hostHooks_{};

ImGuiManager& ImGuiManager::Instance() {
    static ImGuiManager instance;
    return instance;
}

void ImGuiManager::SetHostHooks(const EditorHostHooks& hooks) {
    hostHooks_ = hooks;
}

Scene* ImGuiManager::GetActiveScene() const {
    return hostHooks_.getActiveScene ? hostHooks_.getActiveScene() : nullptr;
}

const char* ImGuiManager::GetActiveSceneName() const {
    return hostHooks_.getActiveSceneName ? hostHooks_.getActiveSceneName() : nullptr;
}

PostEffect* ImGuiManager::GetHostPostEffect() const {
    return hostHooks_.getPostEffect ? hostHooks_.getPostEffect() : nullptr;
}

Framework* ImGuiManager::GetHostFramework() const {
    return hostHooks_.getFramework ? hostHooks_.getFramework() : nullptr;
}

int ImGuiManager::GetEntityGroupCount() const {
    if (!hostHooks_.getEntityGroupCount) return 1;
    const int n = hostHooks_.getEntityGroupCount();
    return (n > 0) ? n : 1;
}

int ImGuiManager::GetEntityGroup(IImGuiEditable* e) const {
    if (!hostHooks_.getEntityGroup || !e) return 0;
    const int g = hostHooks_.getEntityGroup(e);
    return (g >= 0 && g < GetEntityGroupCount()) ? g : 0;
}

const char* ImGuiManager::GetEntityGroupName(int group) const {
    if (!hostHooks_.getEntityGroupName) return "All";
    const char* n = hostHooks_.getEntityGroupName(group);
    return (n && *n) ? n : "(unnamed)";
}

void ImGuiManager::GetEntityGroupColor(int group, float& r, float& g, float& b, float& a) const {
    if (hostHooks_.getEntityGroupColor) {
        hostHooks_.getEntityGroupColor(group, r, g, b, a);
        return;
    }
    r = 0.55f; g = 0.55f; b = 0.60f; a = 1.0f;
}

void ImGuiManager::SetEntityGroup(IImGuiEditable* e, int group) {
    if (hostHooks_.setEntityGroup && e) hostHooks_.setEntityGroup(e, group);
}

Collider* ImGuiManager::GetEntityCollider(IImGuiEditable* e) const {
    if (!e) return nullptr;
    if (hostHooks_.getCollider) return hostHooks_.getCollider(e);
    return &CollisionSystem::GetInstance()->ColliderOf(e);
}

void ImGuiManager::AddInspectorSection(const std::string& name,
    std::function<void(IImGuiEditable*)> draw)
{
#ifdef _DEBUG
    if (draw) inspectorSections_.push_back({ name, std::move(draw) });
#else
    (void)name; (void)draw;
#endif
}

void ImGuiManager::AddAssetBrowserSection(const std::string& name, std::function<void()> draw)
{
#ifdef _DEBUG
    if (draw) assetBrowserSections_.push_back({ name, std::move(draw) });
#else
    (void)name; (void)draw;
#endif
}

void ImGuiManager::AddWindow(std::unique_ptr<IImGuiWindow> window) {
#ifdef _DEBUG
    if (window) windows_.push_back(std::move(window));
#else
    (void)window;
#endif
}

void ImGuiManager::AddCallbackWindow(const std::string& name, std::function<void()> draw) {
#ifdef _DEBUG
    windows_.push_back(std::make_unique<CallbackWindow>(name, std::move(draw)));
#else
    (void)name; (void)draw;
#endif
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
    // 新しい InitInfo 版で初期化し、テクスチャ(フォント)アップロードを
    // 「エンジンのメインキュー」で行わせる。
    // レガシー Init は ImGui 内部で別コマンドキューを作り、そこでフォントを
    // COPY_DEST→PIXEL_SHADER_RESOURCE に遷移するため、メインキューでの描画と
    // クロスキューになり GPU-Based Validation がレイアウト不整合(COPY_DEST)で弾いていた。
    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = dxCore_->GetDevice();
    initInfo.CommandQueue = dxCore_->GetCommandQueue();   // ★クロスキュー解消の要
    initInfo.NumFramesInFlight = dxCore_->GetBackBufferCount();
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // RTVのフォーマットに合わせる
    initInfo.SrvDescriptorHeap = nullptr;                  // SRVヒープはSRVManagerが管理
    initInfo.LegacySingleSrvCpuDescriptor = srvManager_->GetCPUDescriptorHandle(srvIndex_);
    initInfo.LegacySingleSrvGpuDescriptor = srvManager_->GetGPUDescriptorHandle(srvIndex_);
    ImGui_ImplDX12_Init(&initInfo);

    // ImGui::Image 等の複数テクスチャはアプリ(SRVManager)側が SRV を管理するので、
    // バックエンドの動的テクスチャ機能は使わない（レガシー Init と同じ挙動を維持）。
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;

    // 日本語対応フォントを読み込む（無ければ標準フォントにフォールバック）
    // 同梱の M PLUS 1p を使うことで配布先PCでもフォント依存なく日本語表示できる。
    const char* kFontPath = "Resources/Fonts/MPLUS1p-Regular.ttf";
    ImFont* jpFont = io.Fonts->AddFontFromFileTTF(
        kFontPath, 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    if (jpFont == nullptr) {
        io.Fonts->AddFontDefault();
    }

    // フォントアトラスを明示的にビルド（docking版のレガシーAPI対応）
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // 各ウィンドウを生成
    // ViewportWindow は EndFrame のギズモ描画で直接参照するのでポインタを控えておく
    {
        auto viewport = std::make_unique<ViewportWindow>(srvManager_);
        viewportWindow_ = viewport.get();
        windows_.push_back(std::move(viewport));
    }
    windows_.push_back(std::make_unique<FPSWindow>());
    windows_.push_back(std::make_unique<PepperWindow>());
    windows_.push_back(std::make_unique<LogWindow>());
    windows_.push_back(std::make_unique<HierarchyWindow>(this));
    windows_.push_back(std::make_unique<InspectorWindow>(this));

    // デバッグUI群を CallbackWindow 経由で登録
    windows_.push_back(std::make_unique<CallbackWindow>("Camera",
        [this]() {
            // ===== Debug Camera toggle =====
            Scene* scene = GetActiveScene();
            if (scene) {
                bool useDebug = scene->GetUseDebugCamera();
                if (ImGui::Checkbox("Use Debug Camera", &useDebug)) {
                    scene->SetUseDebugCamera(useDebug);
                }
                if (useDebug) {
                    if (DebugCamera* dc = scene->GetDebugCamera()) {
                        ImGui::TextDisabled("L-Drag: Orbit  /  M-Drag: Pan  /  Wheel: Zoom");

                        // ホイール/ドラッグで変わった現在値を毎フレーム反映
                        Vector3 pivot = dc->GetPivot();
                        float pivotV[3] = { pivot.x, pivot.y, pivot.z };
                        if (ImGui::DragFloat3("Pivot", pivotV, 0.1f)) {
                            dc->SetPivot({ pivotV[0], pivotV[1], pivotV[2] });
                        }

                        float distance = dc->GetDistance();
                        if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 100000.0f)) {
                            dc->SetDistance(distance);
                        }

                        float fovYDeg = RadToDeg(dc->GetFovY());
                        if (ImGui::DragFloat("Fov Y (deg)", &fovYDeg, 0.5f, 10.0f, 120.0f)) {
                            dc->SetFovY(DegToRad(fovYDeg));
                        }
                    }
                }
                ImGui::Separator();
            }
            // ===== Scene camera info =====
            if (camera_) camera_->OnImGui();
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("Light",
        [this]() {
            LightManager::GetInstance()->OnImGui();
            // シャドウ（CSM）調整 UI を同じ Light パネルに差し込む
            if (Framework* fw = GetHostFramework()) {
                if (ShadowMap* sm = fw->GetShadowMap()) {
                    sm->OnImGui();
                }
            }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("PostEffect",
        [this]() { if (auto* p = GetHostPostEffect()) p->ShowImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("Particle",
        [this]() {
            if (gpuParticleManager_) {
                gpuParticleManager_->OnImGui();
            } else {
                ImGui::TextDisabled("GPUParticleManager is not active in the current scene.");
            }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("Highlights",
        [this]() {
            Scene* scene = GetActiveScene();
            if (!scene) {
                ImGui::TextDisabled("No active scene.");
                return;
            }
            // PostEffect トグル
            if (auto* pe = GetHostPostEffect(); pe && pe->maskedGrayscale) {
                bool en = pe->maskedGrayscale->IsEnabled();
                if (ImGui::Checkbox("Enable MaskedGrayscale", &en)) {
                    pe->maskedGrayscale->SetEnabled(en);
                }
                float intensity = pe->maskedGrayscale->GetIntensity();
                if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
                    pe->maskedGrayscale->SetIntensity(intensity);
                    pe->maskedGrayscale->UpdateConstantBuffer();
                }
            }
            ImGui::Separator();

            IImGuiEditable* selected = GetSelected();
            ImGui::Text("Selected: %s",
                selected ? selected->GetName().c_str() : "(none)");
            if (ImGui::Button("Add Selected") && selected) {
                scene->AddHighlight(selected);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Selected") && selected) {
                scene->RemoveHighlight(selected);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                scene->ClearHighlights();
            }

            ImGui::Separator();
            ImGui::Text("Highlighted (%d):",
                static_cast<int>(scene->GetHighlights().size()));
            for (auto* e : scene->GetHighlights()) {
                if (e) ImGui::BulletText("[%u] %s",
                    static_cast<unsigned>(e->GetObjectId()),
                    e->GetName().c_str());
            }
        }));
    // Effect Editor（プレビューRT付き）
    auto effectEditor = std::make_unique<EffectEditorWindow>(dxCore_, srvManager_, this);
    effectEditor->Initialize();
    effectEditorWindow_ = effectEditor.get();
    windows_.push_back(std::move(effectEditor));

    // Effect Hierarchy（編集中エフェクトのコンポーネントを種類別表示）
    windows_.push_back(std::make_unique<EffectHierarchyWindow>(this, effectEditorWindow_));
    // Effect Components パレット（コンポーネント追加用の独立した D&D ソースウィンドウ）
    windows_.push_back(std::make_unique<EffectPaletteWindow>());
    windows_.push_back(std::make_unique<CallbackWindow>("TimeControler",
        [this]() {
            Scene* scene = GetActiveScene();
            const char* name = GetActiveSceneName();

            // ===== シーン情報 =====
            ImGui::Text("Current Scene:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s",
                (name && *name) ? name : "(none)");

            ImGui::Separator();

            // ===== シーンタイムライン（シーク） =====
            if (ImGui::CollapsingHeader("Scene Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!scene) {
                    ImGui::TextDisabled("No active scene.");
                } else {
                    float elapsed = scene->GetElapsedSeconds();
                    float camT    = scene->GetCameraProgressT();

                    ImGui::Text("Elapsed: %.2f sec", elapsed);

                    // カメラ進行度。使うかどうかは Scene::GetCameraProgressT の override 次第
                    if (camT >= 0.0f) {
                        ImGui::Text("Camera Progress t: %.3f", camT);
                    }

                    // シークはシーンが総尺を申告している場合だけ有効
                    const float seekMax = scene->GetSeekMaxSeconds();
                    if (seekMax > 0.0f) {
                        float seekValue = elapsed;
                        if (ImGui::SliderFloat("Seek", &seekValue, 0.0f, seekMax, "%.2f sec")) {
                            scene->Seek(seekValue);
                        }
                    } else {
                        ImGui::TextDisabled("Seek: Scene::GetSeekMaxSeconds() を override すると有効になります");
                    }

                    if (ImGui::Button("-1s")) { scene->Seek(elapsed - 1.0f); }
                    ImGui::SameLine();
                    if (ImGui::Button("-0.1s")) { scene->Seek(elapsed - 0.1f); }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset")) { scene->Seek(0.0f); }
                    ImGui::SameLine();
                    if (ImGui::Button("+0.1s")) { scene->Seek(elapsed + 0.1f); }
                    ImGui::SameLine();
                    if (ImGui::Button("+1s")) { scene->Seek(elapsed + 1.0f); }
                }
            }

            ImGui::Spacing();

            // ===== グローバル TimeScale =====
            if (ImGui::CollapsingHeader("Global TimeScale", ImGuiTreeNodeFlags_DefaultOpen)) {
                float globalScale = dxCore_ ? dxCore_->GetTimeScale() : 1.0f;
                if (ImGui::SliderFloat("Global TimeScale", &globalScale, 0.0f, 4.0f, "%.2f")) {
                    if (dxCore_) dxCore_->SetTimeScale(globalScale);
                }
                if (ImGui::Button("Pause##g"))  { if (dxCore_) dxCore_->SetTimeScale(0.0f); }
                ImGui::SameLine();
                if (ImGui::Button("0.25x##g")) { if (dxCore_) dxCore_->SetTimeScale(0.25f); }
                ImGui::SameLine();
                if (ImGui::Button("0.5x##g"))  { if (dxCore_) dxCore_->SetTimeScale(0.5f); }
                ImGui::SameLine();
                if (ImGui::Button("1x##g"))    { if (dxCore_) dxCore_->SetTimeScale(1.0f); }
                ImGui::SameLine();
                if (ImGui::Button("2x##g"))    { if (dxCore_) dxCore_->SetTimeScale(2.0f); }
            }

            ImGui::Spacing();

            // ===== シーン別グループ TimeScale =====
            if (ImGui::CollapsingHeader("Scene Time Groups", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (scene) {
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
                    if (ImGui::Button("UI Only")) {
                        // World/Player/Effect を止めて UI だけ動かす（ポーズ表現の雛形）
                        for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i)
                            scene->SetTimeScale(static_cast<TimeGroup>(i), 0.0f);
                        scene->SetTimeScale(TimeGroup::UI, 1.0f);
                    }

                    ImGui::Spacing();
                    ImGui::Text("dt World=%.4f Player=%.4f UI=%.4f",
                        scene->GetScaledDeltaTime(TimeGroup::World),
                        scene->GetScaledDeltaTime(TimeGroup::Player),
                        scene->GetScaledDeltaTime(TimeGroup::UI));
                } else {
                    ImGui::TextDisabled("No active scene.");
                }
            }
        }));
    windows_.push_back(std::make_unique<CallbackWindow>("WebCam Devices",
        []() { CameraCapture::GetInstance()->LogDevicesToImGui(); }));
    windows_.push_back(std::make_unique<CallbackWindow>("QR Code",
        []() { QRCodeReader::GetInstance()->OnImGui(); }));

    // シーンエディタ（アセット一覧の非同期スキャン + 動的オブジェクト追加・削除）
    windows_.push_back(std::make_unique<SceneEditorWindow>(this));

    // ここまでがエンジン標準のパネル。
    // ゲーム固有パネルはホスト側が Initialize 後に
    // AddWindow / AddCallbackWindow / AddInspectorSection / AddAssetBrowserSection で足す。

    isInitialized_ = true;

#endif // !_DEBUG
}

void ImGuiManager::Shutdown() {
#ifdef _DEBUG
    if (!isInitialized_) return;

    windows_.clear();
    editables_.clear();
    inspectorSections_.clear();
    assetBrowserSections_.clear();
    selectedObject_ = nullptr;
    viewportWindow_ = nullptr;
    effectEditorWindow_ = nullptr;

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
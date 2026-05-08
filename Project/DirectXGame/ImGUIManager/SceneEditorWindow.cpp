#include "SceneEditorWindow.h"
#include "ImGuiManager.h"
#include "ModelManager.h"
#include "PrimitiveInstance.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
    // 文字配列ペイロードへの安全コピー
    inline void SafeCopy(char* dst, size_t cap, const std::string& src) {
        size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
        std::memcpy(dst, src.data(), n);
        dst[n] = '\0';
    }
}

SceneEditorWindow::SceneEditorWindow(ImGuiManager* manager)
    : IImGuiWindow("Scene Editor"), manager_(manager) {
#ifdef _DEBUG
    workerThread_ = std::thread(&SceneEditorWindow::WorkerFunc, this);
#endif
}

SceneEditorWindow::~SceneEditorWindow() {
#ifdef _DEBUG
    stopRequested_ = true;
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
#endif
}

void SceneEditorWindow::WorkerFunc() {
    // バックグラウンドスレッドでの例外はプロセス全体を落とすため try-catch で保護する
    try {
        namespace fs = std::filesystem;

        auto toLowerExt = [](const fs::path& p) {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        };

        // ============================================
        // Phase 1a: Resources/Models 配下を再帰スキャン
        //   .obj                → Models カテゴリ
        //   .gltf / .glb / .fbx → Animated カテゴリ
        //   どちらもフォルダ位置の制約はなし（Player/Enemy フォルダ等にも置ける）
        // ============================================
        const fs::path modelsRoot = fs::path("Resources") / "Models";

        std::vector<ModelEntry> foundModels;
        std::vector<AnimatedEntry> foundAnimated;

        if (fs::exists(modelsRoot)) {
            for (auto it = fs::recursive_directory_iterator(modelsRoot);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (stopRequested_) return;
                if (!it->is_regular_file()) continue;

                const fs::path& p = it->path();
                const std::string ext = toLowerExt(p);

                if (ext == ".obj") {
                    ModelEntry e;
                    e.dirPath = p.parent_path().generic_string();
                    e.filename = p.filename().string();
                    e.displayName = fs::relative(p, modelsRoot).generic_string();
                    foundModels.push_back(std::move(e));
                } else if (ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                    AnimatedEntry e;
                    e.dirPath = p.parent_path().generic_string();
                    e.filename = p.filename().string();
                    e.displayName = fs::relative(p, modelsRoot).generic_string();
                    foundAnimated.push_back(std::move(e));
                }
            }
        }

        // ============================================
        // Phase 1b: Resources/Textures 配下を再帰スキャン（.png）
        // ============================================
        const fs::path texturesRoot = fs::path("Resources") / "Textures";
        std::vector<TextureEntry> foundTextures;
        if (fs::exists(texturesRoot)) {
            for (auto it = fs::recursive_directory_iterator(texturesRoot);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (stopRequested_) return;
                if (!it->is_regular_file()) continue;

                const fs::path& p = it->path();
                const std::string ext = toLowerExt(p);
                if (ext != ".png") continue;

                TextureEntry e;
                e.filePath = p.generic_string();
                e.displayName = fs::relative(p, texturesRoot).generic_string();
                foundTextures.push_back(std::move(e));
            }
        }

        // 結果を共有領域へ
        {
            std::lock_guard<std::mutex> lock(discoveredMutex_);
            discoveredModels_ = std::move(foundModels);
            discoveredTextures_ = std::move(foundTextures);
            discoveredAnimated_ = std::move(foundAnimated);
        }
        scanDone_ = true;

        // ============================================
        // Phase 2: .obj だけ Assimp で CPU 先読み
        // ============================================
        std::vector<ModelEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(discoveredMutex_);
            snapshot = discoveredModels_;
        }
        for (const auto& entry : snapshot) {
            if (stopRequested_) return;
            try {
                ModelManager::GetInstance()->PreloadCPU(entry.dirPath, entry.filename);
            } catch (...) {
                // 1つの失敗で他を止めない
            }
        }
    } catch (...) {
        // スキャン全体での例外は無視
    }
}

void SceneEditorWindow::OnDraw() {
#ifdef _DEBUG

    // ============================================
    // ヘッダー: スキャン状況
    // ============================================
    if (!scanDone_) {
        ImGui::Text("Scanning Resources ...");
    } else {
        std::lock_guard<std::mutex> lock(discoveredMutex_);
        ImGui::Text("Models: %d   Textures: %d   Animated: %d",
            static_cast<int>(discoveredModels_.size()),
            static_cast<int>(discoveredTextures_.size()),
            static_cast<int>(discoveredAnimated_.size()));
    }

    ImGui::Separator();

    // 検索フィルタ
    ImGui::InputText("Search", searchBuf_, sizeof(searchBuf_));
    ImGui::Separator();

    // スナップショットを取ってロック解除
    std::vector<ModelEntry> models;
    std::vector<TextureEntry> textures;
    std::vector<AnimatedEntry> animated;
    {
        std::lock_guard<std::mutex> lock(discoveredMutex_);
        models = discoveredModels_;
        textures = discoveredTextures_;
        animated = discoveredAnimated_;
    }

    auto matchesSearch = [&](const std::string& s) {
        return searchBuf_[0] == '\0' || s.find(searchBuf_) != std::string::npos;
    };

    // ============================================
    // Models セクション（Object3D 用 .obj）
    // ============================================
    if (ImGui::CollapsingHeader("Models  (.obj)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Viewport to add");
        for (size_t i = 0; i < models.size(); ++i) {
            const auto& entry = models[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 100000);

            const bool gpuReady = ModelManager::GetInstance()->IsGPUReady(entry.filename);
            const bool cpuReady = ModelManager::GetInstance()->IsCPUReady(entry.filename);
            const char* icon = gpuReady ? "[v]" : (cpuReady ? "[~]" : "[.]");
            std::string label = std::string(icon) + " " + entry.displayName;

            ImGui::Selectable(label.c_str(), false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ModelDropPayload payload{};
                SafeCopy(payload.dirPath, sizeof(payload.dirPath), entry.dirPath);
                SafeCopy(payload.filename, sizeof(payload.filename), entry.filename);
                ImGui::SetDragDropPayload(MODEL_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }
    }

    // ============================================
    // Sprites セクション（.png）
    // ============================================
    if (ImGui::CollapsingHeader("Sprites  (.png)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Viewport to place at cursor");
        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& entry = textures[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 200000);

            ImGui::Selectable(entry.displayName.c_str(), false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                SpriteDropPayload payload{};
                SafeCopy(payload.texturePath, sizeof(payload.texturePath), entry.filePath);
                ImGui::SetDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }
    }

    // ============================================
    // Primitives セクション（Plane / Box / Sphere / Ring / Cylinder）
    // ============================================
    if (ImGui::CollapsingHeader("Primitives", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Viewport to add");

        const int kPrimitiveCount = static_cast<int>(PrimitiveInstance::PrimitiveType::kCount);
        for (int i = 0; i < kPrimitiveCount; ++i) {
            const auto type = static_cast<PrimitiveInstance::PrimitiveType>(i);
            const char* label = PrimitiveInstance::PrimitiveTypeToString(type);

            ImGui::PushID(i + 400000);

            ImGui::Selectable(label, false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                PrimitiveDropPayload payload{};
                payload.primitiveType = i;
                ImGui::SetDragDropPayload(PRIMITIVE_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }
    }

    // ============================================
    // Animated セクション（.gltf / .glb / .fbx）
    // ============================================
    if (ImGui::CollapsingHeader("Animated  (.gltf / .glb / .fbx)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("scanned across all Resources/Models/ subfolders");
        if (animated.empty()) {
            ImGui::TextDisabled("(none found)");
        }
        for (size_t i = 0; i < animated.size(); ++i) {
            const auto& entry = animated[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 300000);

            ImGui::Selectable(entry.displayName.c_str(), false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                AnimatedDropPayload payload{};
                SafeCopy(payload.dirPath, sizeof(payload.dirPath), entry.dirPath);
                SafeCopy(payload.filename, sizeof(payload.filename), entry.filename);
                ImGui::SetDragDropPayload(ANIMATED_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }
    }

#endif // _DEBUG
}

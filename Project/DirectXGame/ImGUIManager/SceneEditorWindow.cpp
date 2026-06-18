#include "SceneEditorWindow.h"
#include "ImGuiManager.h"
#include "ModelManager.h"
#include "PrimitiveInstance.h"
#include "AssetLocator.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Components/EntityTag.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include "Effect/EffectManager.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

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

        auto* locator = AssetLocator::GetInstance();

        // ============================================
        // Phase 1a: Resources/Models 配下から .mesh / .gltf / .glb / .fbx を列挙
        //   .mesh               → Models カテゴリ
        //   .gltf / .glb / .fbx → Animated カテゴリ
        // ============================================
        const fs::path modelsRoot = fs::path("Resources") / "Models";

        std::vector<ModelEntry> foundModels;
        std::vector<AnimatedEntry> foundAnimated;

        // .mesh ヘッダーの flags bit0 = HAS_SKINNING で Models / Animated を分類
        for (const auto& p : locator->ListByExtension(".mesh", modelsRoot.generic_string())) {
            if (stopRequested_) return;
            fs::path fp(p);

            // ヘッダー先頭 12 バイトを読み、flags を取得
            // [0..3] magic "MESH", [4..7] version, [8..11] flags
            bool hasSkinning = false;
            auto h = locator->Open(p);
            if (h.IsValid()) {
                char hdr[12]{};
                h.Read(hdr, 12);
                if (std::memcmp(hdr, "MESH", 4) == 0) {
                    uint32_t flags = 0;
                    std::memcpy(&flags, hdr + 8, 4);
                    hasSkinning = (flags & 0x1) != 0;
                }
            }

            if (hasSkinning) {
                AnimatedEntry e;
                e.dirPath = fp.parent_path().generic_string();
                e.filename = fp.filename().string();
                e.displayName = fs::relative(fp, modelsRoot).generic_string();
                foundAnimated.push_back(std::move(e));
            } else {
                ModelEntry e;
                e.dirPath = fp.parent_path().generic_string();
                e.filename = fp.filename().string();
                e.displayName = fs::relative(fp, modelsRoot).generic_string();
                foundModels.push_back(std::move(e));
            }
        }

        // 旧フォーマット .gltf/.glb/.fbx も拾えるようにしておく（後方互換）
        for (const char* ext : { ".gltf", ".glb", ".fbx" }) {
            for (const auto& p : locator->ListByExtension(ext, modelsRoot.generic_string())) {
                if (stopRequested_) return;
                fs::path fp(p);
                AnimatedEntry e;
                e.dirPath = fp.parent_path().generic_string();
                e.filename = fp.filename().string();
                e.displayName = fs::relative(fp, modelsRoot).generic_string();
                foundAnimated.push_back(std::move(e));
            }
        }

        // ============================================
        // Phase 1b: Resources/Textures 配下から .dds を列挙
        // ============================================
        const fs::path texturesRoot = fs::path("Resources") / "Textures";
        std::vector<TextureEntry> foundTextures;
        for (const auto& p : locator->ListByExtension(".dds", texturesRoot.generic_string())) {
            if (stopRequested_) return;
            fs::path fp(p);
            TextureEntry e;
            e.filePath = p;
            e.displayName = fs::relative(fp, texturesRoot).generic_string();
            foundTextures.push_back(std::move(e));
        }

        // ============================================
        // Phase 1c: Resources/Models 配下から .mat を列挙
        // ============================================
        std::vector<MaterialEntry> foundMaterials;
        for (const auto& p : locator->ListByExtension(".mat", modelsRoot.generic_string())) {
            if (stopRequested_) return;
            fs::path fp(p);
            MaterialEntry e;
            e.filePath = p;
            e.displayName = fs::relative(fp, modelsRoot).generic_string();
            foundMaterials.push_back(std::move(e));
        }

        // ============================================
        // Phase 1d: Resources/Models 配下から .anim を列挙
        // ============================================
        std::vector<AnimEntry> foundAnims;
        for (const auto& p : locator->ListByExtension(".anim", modelsRoot.generic_string())) {
            if (stopRequested_) return;
            fs::path fp(p);
            AnimEntry e;
            e.filePath = p;
            e.displayName = fs::relative(fp, modelsRoot).generic_string();
            foundAnims.push_back(std::move(e));
        }

        // 結果を共有領域へ
        {
            std::lock_guard<std::mutex> lock(discoveredMutex_);
            discoveredModels_ = std::move(foundModels);
            discoveredTextures_ = std::move(foundTextures);
            discoveredAnimated_ = std::move(foundAnimated);
            discoveredMaterials_ = std::move(foundMaterials);
            discoveredAnims_ = std::move(foundAnims);
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

void SceneEditorWindow::RefreshEffectsIfChanged() {
    namespace fs = std::filesystem;
    const fs::path effectsDir = fs::path("Resources") / "Json" / "Effects";
    std::error_code ec;
    if (!fs::exists(effectsDir, ec)) {
        // ディレクトリ未作成。一覧は空のまま
        if (!discoveredEffects_.empty()) discoveredEffects_.clear();
        return;
    }

    // ディレクトリ自体の最終書込時刻でホットリロード判定（ファイル追加/削除/書込で更新される）
    auto lwt = fs::last_write_time(effectsDir, ec);
    if (ec) return;

    if (effectsInitialized_ && lwt == effectsLastWriteTime_) {
        return; // 変化なし
    }
    effectsLastWriteTime_ = lwt;
    effectsInitialized_ = true;

    // EffectManager に再ロードさせる（保存済み .json → defs_ にロード）
    EffectManager::GetInstance()->LoadAllDefsInDirectory(effectsDir.generic_string());

    // 一覧を再構築
    discoveredEffects_.clear();
    for (const auto& entry : fs::directory_iterator(effectsDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        EffectEntry e;
        e.displayName = entry.path().stem().string();
        e.filePath    = entry.path().generic_string();
        discoveredEffects_.push_back(std::move(e));
    }
    std::sort(discoveredEffects_.begin(), discoveredEffects_.end(),
        [](const EffectEntry& a, const EffectEntry& b) { return a.displayName < b.displayName; });
}

void SceneEditorWindow::OnDraw() {
#ifdef _DEBUG

    // エフェクト一覧をホットリロード（ディレクトリ変更検出時のみ実行されるので軽い）
    RefreshEffectsIfChanged();

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

    // ============================================
    // シーン保存 / 読込
    // ============================================
    {
        static char scenePathBuf[256] = "Resources/Json/Scenes/StagePlay.json";
        ImGui::InputText("Scene File", scenePathBuf, sizeof(scenePathBuf));
        if (ImGui::Button("Save Scene")) {
            if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                scene->SaveSceneToJson(scenePathBuf);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Scene")) {
            if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                scene->LoadSceneFromJson(scenePathBuf);
            }
        }
    }
    ImGui::Separator();

    // ============================================
    // プリファブ一覧（ボタンをドラッグ元として使う）
    // ============================================
    {
        // 初回だけ自動スキャン
        static bool prefabScanned = false;
        if (!prefabScanned) {
            PrefabManager::GetInstance()->Rescan();
            prefabScanned = true;
        }
        ImGui::TextUnformatted("Prefabs:");
        ImGui::SameLine();
        if (ImGui::Button("Rescan##prefabs")) {
            PrefabManager::GetInstance()->Rescan();
        }
        const auto& prefabs = PrefabManager::GetInstance()->GetAll();
        if (prefabs.empty()) {
            ImGui::TextDisabled("(none in %s)", PrefabManager::GetPrefabDir());
        } else {
            // 削除確認ポップアップ対象（ループ中に Rescan() すると参照が無効化されるため遅延処理する）
            static std::string prefabToDelete;
            for (const auto& p : prefabs) {
                ImGui::PushID(p.name.c_str());
                ImGui::Button(p.name.c_str());
                if (ImGui::BeginDragDropSource()) {
                    PrefabDropPayload pld{};
                    SafeCopy(pld.prefabName, sizeof(pld.prefabName), p.name);
                    ImGui::SetDragDropPayload(PREFAB_DROP_PAYLOAD_TYPE, &pld, sizeof(pld));
                    ImGui::Text("Prefab: %s", p.name.c_str());
                    ImGui::TextDisabled("[%s] %s/%s",
                        std::string(GetTagName(p.tag)).c_str(),
                        p.modelDir.c_str(), p.modelFile.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    prefabToDelete = p.name;
                    ImGui::OpenPopup("Delete Prefab?");
                }
                if (ImGui::BeginPopupModal("Delete Prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete prefab \"%s\" ?", prefabToDelete.c_str());
                    ImGui::TextDisabled("This deletes the .json file and cannot be undone.");
                    ImGui::Separator();
                    if (ImGui::Button("Delete", ImVec2(120, 0))) {
                        if (PrefabManager::Delete(prefabToDelete)) {
                            PrefabManager::GetInstance()->Rescan();
                        }
                        prefabToDelete.clear();
                        ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break; // prefabs 参照が無効化されたのでループを抜ける
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        prefabToDelete.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::Separator();

    // ============================================
    // スプライン追加（役割タグごとに4ボタン）
    // ============================================
    {
        ImGui::TextUnformatted("Add Spline:");
        struct SplineKind {
            const char* label;
            EntityTag tag;
        };
        const SplineKind kinds[] = {
            { "PlayerRail",   EntityTag::PlayerRailSpline },
            { "EnemyPath",    EntityTag::EnemyPathSpline },
            { "FloatingPath", EntityTag::FloatingPathSpline },
            { "CameraPath",   EntityTag::CameraPathSpline },
        };
        for (size_t i = 0; i < std::size(kinds); ++i) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::Button(kinds[i].label)) {
                if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                    scene->AddDynamicSpline(static_cast<int>(kinds[i].tag));
                }
            }
        }
    }
    ImGui::Separator();

    // 検索フィルタ
    ImGui::InputText("Search", searchBuf_, sizeof(searchBuf_));
    ImGui::Separator();

    // スナップショットを取ってロック解除
    std::vector<ModelEntry> models;
    std::vector<TextureEntry> textures;
    std::vector<AnimatedEntry> animated;
    std::vector<MaterialEntry> materials;
    std::vector<AnimEntry> anims;
    {
        std::lock_guard<std::mutex> lock(discoveredMutex_);
        models = discoveredModels_;
        textures = discoveredTextures_;
        animated = discoveredAnimated_;
        materials = discoveredMaterials_;
        anims = discoveredAnims_;
    }

    auto matchesSearch = [&](const std::string& s) {
        return searchBuf_[0] == '\0' || s.find(searchBuf_) != std::string::npos;
    };

    // ============================================
    // Models セクション（Object3D 用 .mesh）
    // ============================================
    if (ImGui::CollapsingHeader("Models  (.mesh)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Scene to add");
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
    // Sprites セクション（.dds）
    // ============================================
    if (ImGui::CollapsingHeader("Sprites  (.dds)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Scene to place at cursor");
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
    // Materials セクション（.mat）
    // ============================================
    if (ImGui::CollapsingHeader("Materials  (.mat)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto Object3D/Animated Inspector to apply");
        for (size_t i = 0; i < materials.size(); ++i) {
            const auto& entry = materials[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 500000);

            ImGui::Selectable(entry.displayName.c_str(), false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                MaterialDropPayload payload{};
                SafeCopy(payload.materialPath, sizeof(payload.materialPath), entry.filePath);
                ImGui::SetDragDropPayload(MATERIAL_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
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
        ImGui::TextDisabled("drag onto Scene to add");

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

    // Effect Components パレットは EffectHierarchyWindow（エンジン側）へ移設済み。

    // ============================================
    // Animated セクション（.gltf / .glb / .fbx）
    // ============================================
    // ============================================
    // Animations セクション（.anim）— AnimatedObject3D Inspector へ D&D で適用
    // ============================================
    if (ImGui::CollapsingHeader("Animations  (.anim)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("drag onto AnimatedObject's Animation slot");
        if (anims.empty()) {
            ImGui::TextDisabled("(none found)");
        }
        for (size_t i = 0; i < anims.size(); ++i) {
            const auto& entry = anims[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 400000);
            ImGui::Selectable(entry.displayName.c_str(), false);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                AnimDropPayload payload{};
                SafeCopy(payload.animPath, sizeof(payload.animPath), entry.filePath);
                ImGui::SetDragDropPayload(ANIM_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Animated  (.mesh w/ skin)", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    // ============================================
    // Effects（Effect Editor で保存したエフェクトの一覧。DnD でプレハブスロットへ）
    // ============================================
    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Resources/Json/Effects/  (saved via Effect Editor)");
        if (discoveredEffects_.empty()) {
            ImGui::TextDisabled("(none found)");
        }
        for (size_t i = 0; i < discoveredEffects_.size(); ++i) {
            const auto& entry = discoveredEffects_[i];
            if (!matchesSearch(entry.displayName)) continue;

            ImGui::PushID(static_cast<int>(i) + 600000);

            ImGui::Selectable(entry.displayName.c_str(), false);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                EffectResDropPayload payload{};
                SafeCopy(payload.effectName, sizeof(payload.effectName), entry.displayName);
                ImGui::SetDragDropPayload(EFFECT_RES_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }
    }

#endif // _DEBUG
}
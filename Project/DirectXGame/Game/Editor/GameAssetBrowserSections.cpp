#include "GameAssetBrowserSections.h"

#include "ImGuiManager.h"
#include "SceneEditorWindow.h"
#include "Scene.h"
#include "Components/EntityTag.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"

#include "imgui.h"

#include <string>
#include <vector>
#include <iterator>
#include <cstring>

namespace {

// 文字配列ペイロードへの安全コピー（D&D ペイロードは固定長 char 配列のため）
inline void SafeCopy(char* dst, size_t cap, const std::string& src) {
    size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

/// プレハブ一覧（ボタンをドラッグ元として使う）
void DrawPrefabs() {
#ifdef _DEBUG
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
#endif
}

/// スプライン追加（役割タグごとに4ボタン）
void DrawSplines() {
#ifdef _DEBUG
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
                if (auto* scene = ImGuiManager::Instance().GetActiveScene()) {
                    scene->AddDynamicSpline(static_cast<int>(kinds[i].tag));
                }
            }
        }
    }
#endif
}

} // namespace

namespace GameAssetBrowserSections {

void Register() {
    auto& imgui = ImGuiManager::Instance();
    imgui.AddAssetBrowserSection("Prefabs", &DrawPrefabs);
    imgui.AddAssetBrowserSection("Splines", &DrawSplines);
}

} // namespace GameAssetBrowserSections

#include "EffectManager.h"
#include "GPUParticleManager.h"
#include "Camera.h"
#include "Log.h"
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

EffectManager* EffectManager::GetInstance() {
    static EffectManager instance;
    return &instance;
}

void EffectManager::Initialize(GPUParticleManager* gpuParticleManager) {
    gpuParticleManager_ = gpuParticleManager;
    defs_.clear();
    activeInstances_.clear();
    pendingDelete_.clear();
    handleToInstance_.clear();
    previewInstances_.clear();
    previewPendingDelete_.clear();
    previewHandleToInstance_.clear();
    nextHandle_ = 1;
}

void EffectManager::Finalize() {
    StopAll();
    StopAllPreview();
    // シーン終了時は GPU が待たれている前提なので pendingDelete もここで安全に解放
    pendingDelete_.clear();
    previewPendingDelete_.clear();
    defs_.clear();
    handleToInstance_.clear();
    previewHandleToInstance_.clear();
}

//==========================================================
// 定義の登録
//==========================================================

void EffectManager::RegisterDef(const EffectDef& def) {
    if (def.name.empty()) {
        Log("EffectManager: RegisterDef skipped — name is empty");
        return;
    }
    defs_[def.name] = def;
}

bool EffectManager::LoadDef(const std::string& filePath) {
    EffectDef def{};
    if (!EffectDefIO::LoadFromFile(filePath, def)) {
        Log(std::string("EffectManager: LoadDef failed — ") + filePath);
        return false;
    }
    if (def.name.empty()) {
        Log(std::string("EffectManager: LoadDef skipped — no name in ") + filePath);
        return false;
    }
    defs_[def.name] = std::move(def);
    return true;
}

void EffectManager::LoadAllDefsInDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (path.extension() != ".json") continue;
        LoadDef(path.string());
    }
}

bool EffectManager::HasDef(const std::string& name) const {
    return defs_.find(name) != defs_.end();
}

const EffectDef* EffectManager::FindDef(const std::string& name) const {
    auto it = defs_.find(name);
    return it == defs_.end() ? nullptr : &it->second;
}

EffectDef* EffectManager::FindDefMutable(const std::string& name) {
    auto it = defs_.find(name);
    return it == defs_.end() ? nullptr : &it->second;
}

std::string EffectManager::SaveDef(EffectDef def, bool allowOverwrite) {
    std::string baseName = def.name.empty() ? std::string("Untitled") : def.name;
    std::string finalName = baseName;

    // allowOverwrite=false の場合、衝突したらサフィックスを付ける
    if (!allowOverwrite) {
        int suffix = 1;
        while (defs_.find(finalName) != defs_.end()) {
            finalName = baseName + "(" + std::to_string(suffix++) + ")";
        }
    }
    def.name = finalName;

    const std::string filePath = "Resources/Json/Effects/" + finalName + ".json";
    if (!EffectDefIO::SaveToFile(filePath, def)) {
        Log(std::string("EffectManager: SaveToFile failed for ") + filePath);
        return std::string();
    }

    defs_[finalName] = std::move(def);
    return finalName;
}

void EffectManager::UnregisterDef(const std::string& name) {
    defs_.erase(name);
}

std::vector<std::string> EffectManager::ListDefNames() const {
    std::vector<std::string> names;
    names.reserve(defs_.size());
    for (const auto& pair : defs_) names.push_back(pair.first);
    return names;
}

//==========================================================
// 再生
//==========================================================

EffectHandle EffectManager::Play(const std::string& effectName, const Vector3& worldPos) {
    const EffectDef* def = FindDef(effectName);
    if (!def) {
        Log(std::string("EffectManager: Play — no def for ") + effectName);
        return kInvalidEffectHandle;
    }
    auto inst = std::make_unique<EffectInstance>();
    inst->Initialize(*def, worldPos, gpuParticleManager_);
    EffectHandle h = nextHandle_++;
    inst->SetHandle(h);
    handleToInstance_[h] = inst.get();
    activeInstances_.push_back(std::move(inst));
    return h;
}

EffectHandle EffectManager::PlayWithDef(const EffectDef& def, const Vector3& worldPos) {
    auto inst = std::make_unique<EffectInstance>();
    inst->Initialize(def, worldPos, gpuParticleManager_);
    EffectHandle h = nextHandle_++;
    inst->SetHandle(h);
    handleToInstance_[h] = inst.get();
    activeInstances_.push_back(std::move(inst));
    return h;
}

EffectHandle EffectManager::PlayPreview(const EffectDef& def, const Vector3& worldPos) {
    auto inst = std::make_unique<EffectInstance>();
    inst->SetPreview(true); // GPUパーティクルを $preview$ グループへ隔離する
    inst->Initialize(def, worldPos, gpuParticleManager_);
    EffectHandle h = nextHandle_++;
    inst->SetHandle(h);
    previewHandleToInstance_[h] = inst.get();
    previewInstances_.push_back(std::move(inst));
    return h;
}

void EffectManager::StopAllPreview() {
    for (auto& inst : previewInstances_) {
        if (inst) inst->Cleanup();
    }
    // Primitive renderer の解放は Draw のコマンドより遅らせる（次フレーム頭で解放）。
    for (auto& inst : previewInstances_) {
        if (inst) previewPendingDelete_.push_back(std::move(inst));
    }
    previewInstances_.clear();
    previewHandleToInstance_.clear();
}

void EffectManager::Stop(EffectHandle handle) {
    auto it = handleToInstance_.find(handle);
    if (it == handleToInstance_.end()) return;
    if (it->second) it->second->RequestStop();
}

void EffectManager::SetPosition(EffectHandle handle, const Vector3& pos) {
    auto it = handleToInstance_.find(handle);
    if (it == handleToInstance_.end()) return;
    if (it->second) it->second->SetWorldPosition(pos);
}

void EffectManager::SetRotation(EffectHandle handle, const Vector3& rotate) {
    auto it = handleToInstance_.find(handle);
    if (it == handleToInstance_.end()) return;
    if (it->second) it->second->SetWorldRotation(rotate);
}

bool EffectManager::IsAlive(EffectHandle handle) const {
    return handleToInstance_.find(handle) != handleToInstance_.end();
}

void EffectManager::StopAll() {
    // ライト等のスロット予約はその場で解放してOK（GPU描画が参照しないため）。
    for (auto& inst : activeInstances_) {
        if (inst) inst->Cleanup();
    }
    // Primitive renderer の解放だけは Draw のコマンドより遅らせる必要があるので
    // 次フレーム頭まで pendingDelete_ で保持する。
    for (auto& inst : activeInstances_) {
        if (inst) pendingDelete_.push_back(std::move(inst));
    }
    activeInstances_.clear();
    handleToInstance_.clear();
}

//==========================================================
// 毎フレーム
//==========================================================

void EffectManager::Update(float deltaTime) {
    // 前フレームの「描画後に解放したい」インスタンスをここで安全に破棄。
    // ここに来た時点で GPU は前フレームのコマンド消化を待ち合わせ済み（フレーム境界）。
    pendingDelete_.clear();

    // 各インスタンスを更新
    for (auto& inst : activeInstances_) {
        if (inst) inst->Update(camera_, deltaTime);
    }

    // 終了したインスタンスを取り出して pendingDelete_ へ移送（即破棄しない）。
    for (auto it = activeInstances_.begin(); it != activeInstances_.end();) {
        if (!(*it) || (*it)->IsFinished()) {
            if (*it) {
                handleToInstance_.erase((*it)->GetHandle());
                (*it)->Cleanup(); // ライトはここで解放してOK
                pendingDelete_.push_back(std::move(*it));
            }
            it = activeInstances_.erase(it);
        } else {
            ++it;
        }
    }
}

void EffectManager::UpdatePreview(float deltaTime) {
    // 前フレームの描画後に解放予約したインスタンスをここで安全に破棄。
    previewPendingDelete_.clear();

    for (auto& inst : previewInstances_) {
        if (inst) inst->Update(previewCamera_ ? previewCamera_ : camera_, deltaTime);
    }

    for (auto it = previewInstances_.begin(); it != previewInstances_.end();) {
        if (!(*it)) {
            it = previewInstances_.erase(it);
            continue;
        }
        if ((*it)->IsFinished()) {
            // ループ表示 ON なら消さずに巻き戻して再生し続ける（編集中の常時プレビュー）。
            if (previewLoop_) {
                (*it)->Rewind();
                ++it;
                continue;
            }
            previewHandleToInstance_.erase((*it)->GetHandle());
            (*it)->Cleanup();
            previewPendingDelete_.push_back(std::move(*it));
            it = previewInstances_.erase(it);
        } else {
            ++it;
        }
    }

    // GPUパーティクルのプレビューグループをエディタの unscaled delta で独立シミュレートし、
    // 今プレビュー中のエフェクトが使わなくなったグループは遅延リサイクルする（working set を有界化）。
    if (gpuParticleManager_) {
        std::vector<std::string> keepNames;
        for (const auto& inst : previewInstances_) {
            if (inst) inst->CollectPreviewGroupNames(keepNames);
        }
        gpuParticleManager_->UpdatePreviewSim(deltaTime);
        gpuParticleManager_->RecyclePreviewGroups(keepNames);
    }
}

void EffectManager::SyncPreview(const EffectDef& def, const Vector3& worldPos) {
    for (auto& inst : previewInstances_) {
        if (!inst) continue;
        inst->SyncFromDef(def);
        inst->SetWorldPosition(worldPos);
    }
}

void EffectManager::RewindPreview() {
    for (auto& inst : previewInstances_) {
        if (inst) inst->Rewind();
    }
}

void EffectManager::SeekPreview(float targetTime) {
    if (targetTime < 0.0f) targetTime = 0.0f;
    const float step = 1.0f / 60.0f;
    Camera* cam = previewCamera_ ? previewCamera_ : camera_;
    for (auto& inst : previewInstances_) {
        if (!inst) continue;
        inst->Rewind();
        float remaining = targetTime;
        // ステップ数の暴走ガード（長尺シークでもフレームを潰しすぎない）。
        for (int guard = 0; remaining > 1e-5f && guard < 4000; ++guard) {
            const float dt = (remaining > step) ? step : remaining;
            inst->Update(cam, dt);
            if (inst->IsFinished()) break; // 寿命を終えたらそこで停止
            remaining -= dt;
        }
    }
}

void EffectManager::Draw() {
    for (auto& inst : activeInstances_) {
        if (inst) inst->Draw();
    }
}

void EffectManager::DrawDistortionPass() {
    for (auto& inst : activeInstances_) {
        if (inst) inst->DrawDistortionPass();
    }
}

void EffectManager::DrawDistortionPassPreview() {
    for (auto& inst : previewInstances_) {
        if (inst) inst->DrawDistortionPassPreview();
    }
}

bool EffectManager::HasActiveDistortionSource() const {
    for (const auto& inst : activeInstances_) {
        if (inst && inst->HasActiveDistortionSource()) return true;
    }
    return false;
}

void EffectManager::DrawPreview() {
    if (!previewCamera_) return;

    const Matrix4x4 viewMatrix = previewCamera_->GetViewMatrix();
    const Matrix4x4 viewProjectionMatrix = previewCamera_->GetViewProjectionMatrix();
    const Vector3 cameraPos = previewCamera_->GetTranslate();

    // 1. 各Primitive renderer のプレビューWVPを再計算
    for (auto& inst : previewInstances_) {
        if (inst) inst->UpdatePreviewWVP(viewMatrix, viewProjectionMatrix, cameraPos);
    }
    // 2. GPU Particle のプレビュー用PerViewを更新
    if (gpuParticleManager_) {
        gpuParticleManager_->UpdatePreviewView(viewMatrix, viewProjectionMatrix, cameraPos);
    }

    // 3. 描画。Primitive → GPU Particle の順（メインと同順）
    for (auto& inst : previewInstances_) {
        if (inst) inst->DrawPreview();
    }
    if (gpuParticleManager_) {
        gpuParticleManager_->DrawPreview();
    }
}

void EffectManager::OnImGui() {
#ifdef USE_IMGUI
    ImGui::Text("Active Instances: %zu", activeInstances_.size());
    ImGui::Text("Registered Defs: %zu", defs_.size());
    if (ImGui::Button("Stop All")) {
        StopAll();
    }
    ImGui::Separator();

    // 再生位置（テスト用）
    static float playPos[3] = { 0.0f, 1.0f, 0.0f };
    ImGui::DragFloat3("Play Position", playPos, 0.1f);
    ImGui::Separator();

    // 登録済みエフェクトの一覧と Play ボタン
    for (auto& pair : defs_) {
        ImGui::PushID(pair.first.c_str());
        ImGui::Text("%s", pair.first.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Play")) {
            Play(pair.first, Vector3{ playPos[0], playPos[1], playPos[2] });
        }
        ImGui::PopID();
    }
#endif
}

#pragma once
#include "EffectDef.h"
#include "EffectInstance.h"
#include "Vector3.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Camera;
class GPUParticleManager;

/// <summary>
/// エフェクト定義の登録・再生・更新を担うシングルトン。
/// </summary>
class EffectManager {
public:
    static EffectManager* GetInstance();

    void Initialize(GPUParticleManager* gpuParticleManager);
    void Finalize();

    void SetCamera(Camera* camera) { camera_ = camera; }

    // Effect Editor のプレビュー用カメラ。同じインスタンスをプレビュー RT にも描画する。
    void SetPreviewCamera(Camera* previewCamera) { previewCamera_ = previewCamera; }

    // ===== 定義の登録 =====
    /// <summary>
    /// 既にdefを構築済みの場合はそのまま登録（コピー）
    /// </summary>
    void RegisterDef(const EffectDef& def);

    /// <summary>
    /// JSONファイルから読み込んで登録。defのnameが空ならファイル名をフォールバック使用しない
    /// （EffectDef.name を使う）
    /// </summary>
    bool LoadDef(const std::string& filePath);

    /// <summary>
    /// ディレクトリ内の*.jsonをすべてLoadDef（再帰なし）
    /// </summary>
    void LoadAllDefsInDirectory(const std::string& dirPath);

    bool HasDef(const std::string& name) const;
    const EffectDef* FindDef(const std::string& name) const;

    // ===== 再生 =====
    /// <summary>
    /// 指定名のエフェクトを worldPos に再生開始。defがなければ何もしない。
    /// </summary>
    void Play(const std::string& effectName, const Vector3& worldPos);

    /// <summary>
    /// 再生中のすべてのエフェクトを即終了させる
    /// </summary>
    void StopAll();

    // ===== 毎フレーム =====
    void Update(float deltaTime);
    void Draw();

    // プレビュー RT への描画。SetPreviewCamera で設定したカメラ視点で同じインスタンスを描画する。
    // 呼び出し前にプレビュー RT を BeginRender 済みであること。
    void DrawPreview();

    // ImGui デバッグUI（登録済みエフェクトと再生中インスタンス数を表示）
    void OnImGui();

    // 再生中インスタンス数（デバッグ用）
    size_t GetActiveInstanceCount() const { return activeInstances_.size(); }

    // 登録済みエフェクト名一覧（デバッグ用）
    std::vector<std::string> ListDefNames() const;

private:
    EffectManager() = default;
    ~EffectManager() = default;
    EffectManager(const EffectManager&) = delete;
    EffectManager& operator=(const EffectManager&) = delete;

    GPUParticleManager* gpuParticleManager_ = nullptr;
    Camera* camera_ = nullptr;
    Camera* previewCamera_ = nullptr;

    std::unordered_map<std::string, EffectDef> defs_;
    std::vector<std::unique_ptr<EffectInstance>> activeInstances_;

    // 破棄予約：このフレームの Draw で参照されたPrimitiveMeshリソースを
    // CloseCommandList より先に解放すると D3D12 ERROR #921 になるため、
    // 次フレームの Update 冒頭まで保持する。
    std::vector<std::unique_ptr<EffectInstance>> pendingDelete_;
};

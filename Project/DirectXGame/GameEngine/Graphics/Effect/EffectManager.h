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
/// 再生中のエフェクトを外部から制御するためのハンドル。
/// Play 系 API が返す値で、Stop / SetPosition / IsAlive に使う。
/// 0 は無効値。
/// </summary>
using EffectHandle = uint64_t;
constexpr EffectHandle kInvalidEffectHandle = 0;

/// <summary>
/// エフェクト定義の登録・再生・更新を担うシングルトン。
/// </summary>
class EffectManager {
public:
    static EffectManager* GetInstance();

    void Initialize(GPUParticleManager* gpuParticleManager);
    void Finalize();

    // Initialize で受け取った GPUParticleManager を返す（所有元は Game）。
    // エンジン側 Scene が Game.h に依存せず GPU パーティクルを駆動するために使う。
    GPUParticleManager* GetGPUParticleManager() const { return gpuParticleManager_; }

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

    /// <summary>
    /// 編集用にミュータブル参照を返す（Effect Editor 用）。無ければ nullptr。
    /// </summary>
    EffectDef* FindDefMutable(const std::string& name);

    /// <summary>
    /// def を Resources/Json/Effects/ に保存し、メモリ上の defs_ にも同名で登録する。
    /// allowOverwrite=false の場合、既存名と衝突したら (1)(2)... を付与してユニーク化。
    /// 編集中のものを保存（同名で上書き）したい場合は allowOverwrite=true を渡す。
    /// 最終的に使用された名前を返す。
    /// </summary>
    std::string SaveDef(EffectDef def, bool allowOverwrite = false);

    /// <summary>
    /// 登録済み定義を削除（ファイル削除はしない）。
    /// </summary>
    void UnregisterDef(const std::string& name);

    // ===== 再生 =====
    /// <summary>
    /// 指定名のエフェクトを worldPos に再生開始。defがなければ kInvalidEffectHandle。
    /// 戻り値は外部から Stop / SetPosition に使うハンドル。
    /// </summary>
    EffectHandle Play(const std::string& effectName, const Vector3& worldPos);

    /// <summary>
    /// 指定 def をそのまま使ってインスタンス再生（未保存の editBuffer プレビュー用）。
    /// EffectManager の defs_ には登録されない。
    /// </summary>
    EffectHandle PlayWithDef(const EffectDef& def, const Vector3& worldPos);

    // ===== プレビュー専用（EffectEditor 用、ゲームと完全分離） =====
    /// <summary>
    /// 指定 def をプレビュー専用リストで再生する。activeInstances_（ゲーム）には
    /// 一切混ざらないため、シーン描画に漏れず、エディタが自前タイムラインで更新する。
    /// </summary>
    EffectHandle PlayPreview(const EffectDef& def, const Vector3& worldPos);

    /// <summary>
    /// プレビューインスタンスだけを更新する。エディタが unscaled な実 delta で
    /// 毎フレーム呼ぶ（シーンのタイムスケールに依存しない）。
    /// </summary>
    void UpdatePreview(float deltaTime);

    /// <summary>
    /// プレビューインスタンスだけを全停止する（ゲームの activeInstances_ には触れない）。
    /// </summary>
    void StopAllPreview();

    /// <summary>
    /// 全プレビューインスタンスへ編集中 def をライブ反映し、中心位置を追従させる。
    /// エディタが editBuffer の変化に応じて毎フレーム呼ぶ。
    /// </summary>
    void SyncPreview(const EffectDef& def, const Vector3& worldPos);

    /// <summary>
    /// 全プレビューインスタンスを t=0 へ巻き戻す（「Restart」の実体）。
    /// </summary>
    void RewindPreview();

    /// <summary>
    /// プレビューを指定時刻へシークする。完全ランダムアクセスは不可なので
    /// 一旦 Rewind して固定ステップで targetTime まで早送り再シミュレートする。
    /// （パーティクル burst / 出現時ランダムは厳密再現できない点に注意）
    /// </summary>
    void SeekPreview(float targetTime);

    /// <summary>
    /// プレビューのループ表示。ON のとき、寿命を終えたプレビューを削除せず自動 Rewind して
    /// 編集中ずっと再生し続ける（既定 ON）。
    /// </summary>
    void SetPreviewLoop(bool loop) { previewLoop_ = loop; }
    bool GetPreviewLoop() const { return previewLoop_; }

    /// <summary>
    /// 指定ハンドルのエフェクトが再生中なら停止要求を出す（次フレームで終了）。
    /// loop エフェクトを外から終わらせる場合に使う。
    /// </summary>
    void Stop(EffectHandle handle);

    /// <summary>
    /// 指定ハンドルのエフェクト中心位置を更新（弾丸の追従用）。
    /// </summary>
    void SetPosition(EffectHandle handle, const Vector3& pos);

    /// <summary>
    /// 指定ハンドルのエフェクト全体の向きを更新（オイラー角・ラジアン）。
    /// 弾の進行方向にエフェクトを向ける等に使う。
    /// </summary>
    void SetRotation(EffectHandle handle, const Vector3& rotate);

    /// <summary>
    /// 指定ハンドルが現在もアクティブなインスタンスを指しているか
    /// </summary>
    bool IsAlive(EffectHandle handle) const;

    /// <summary>
    /// 再生中のすべてのエフェクトを即終了させる
    /// </summary>
    void StopAll();

    // ===== 毎フレーム =====
    void Update(float deltaTime);
    void Draw();

    /// <summary>
    /// アクティブな全 EffectInstance の useDistortion な Primitive を distortionRT に描画する。
    /// Game::Draw が IdPass の後に呼ぶ想定。RTV/DSV/Viewport/Scissor は呼び出し側が設定済みであること。
    /// </summary>
    void DrawDistortionPass();

    /// <summary>
    /// プレビュー版 distortion パス。EffectEditor のプレビュー RT に対して、プレビュー WVP CB で描画する。
    /// 呼ぶ直前に DrawPreview() でプレビュー WVP が更新されている前提。
    /// </summary>
    void DrawDistortionPassPreview();

    /// <summary>
    /// アクティブな EffectInstance の中に「useDistortion + テクスチャ済」な primitive が1つでもあるか。
    /// Game.cpp / EffectEditorWindow が毎フレーム distortion パスをスキップするか判定するのに使う。
    /// </summary>
    bool HasActiveDistortionSource() const;

    // プレビュー RT への描画。SetPreviewCamera で設定したカメラ視点で同じインスタンスを描画する。
    // 呼び出し前にプレビュー RT を BeginRender 済みであること。
    void DrawPreview();

    // ImGui デバッグUI（登録済みエフェクトと再生中インスタンス数を表示）
    void OnImGui();

    // 再生中インスタンス数（デバッグ用）
    size_t GetActiveInstanceCount() const { return activeInstances_.size(); }

    /// <summary>
    /// 先頭の再生中インスタンスを返す（Timeline表示用）。無ければnullptr。
    /// </summary>
    EffectInstance* GetFirstActiveInstance() {
        for (auto& inst : activeInstances_) if (inst) return inst.get();
        return nullptr;
    }

    /// <summary>
    /// 指定ハンドルの再生中インスタンスを返す（EffectEditor のプレビュー Timeline 表示用）。無ければnullptr。
    /// </summary>
    EffectInstance* GetInstanceByHandle(EffectHandle handle) {
        auto pit = previewHandleToInstance_.find(handle);
        if (pit != previewHandleToInstance_.end()) return pit->second;
        auto it = handleToInstance_.find(handle);
        return (it != handleToInstance_.end()) ? it->second : nullptr;
    }

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

    // ハンドル発行用カウンタ（0は無効値なので 1 から開始）。
    // プレビューとゲームでハンドル空間は共有（重複しない）。
    EffectHandle nextHandle_ = 1;

    // ハンドル → activeInstances_ の生ポインタ。インスタンス破棄時に erase する。
    std::unordered_map<EffectHandle, EffectInstance*> handleToInstance_;

    // 破棄予約：このフレームの Draw で参照されたPrimitiveMeshリソースを
    // CloseCommandList より先に解放すると D3D12 ERROR #921 になるため、
    // 次フレームの Update 冒頭まで保持する。
    std::vector<std::unique_ptr<EffectInstance>> pendingDelete_;

    // ===== プレビュー専用（EffectEditor）。activeInstances_ とは別系統で、
    // シーンの Update/Draw には一切載らない。更新はエディタの UpdatePreview のみ。
    std::vector<std::unique_ptr<EffectInstance>> previewInstances_;
    std::unordered_map<EffectHandle, EffectInstance*> previewHandleToInstance_;
    std::vector<std::unique_ptr<EffectInstance>> previewPendingDelete_;

    // プレビューを寿命終了で消さず自動 Rewind してループ表示するか（編集中の常時プレビュー用）。
    bool previewLoop_ = true;
};

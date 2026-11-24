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
/// シングルトンにする理由：再生中のエフェクトをシーンをまたいで 1 か所で更新・描画し、ハンドルで操作できるようにするため。
/// </summary>
class EffectManager {
public:
    /// <summary>シングルトンを返す</summary>
    /// <returns>唯一のインスタンス</returns>
    static EffectManager* GetInstance();

    /// <summary>定義・再生中・破棄待ち・プレビューをすべて空にし、ハンドルを 1 から振り直す</summary>
    /// <param name="gpuParticleManager">Particle コンポーネントの発射先（所有元は Game）</param>
    void Initialize(GPUParticleManager* gpuParticleManager);
    /// <summary>ゲーム・プレビューの再生をすべて止め、破棄待ちと定義を解放する（GPU が待たれている前提）</summary>
    void Finalize();

    /// <summary>
    /// Initialize で受け取った GPUParticleManager を返す（所有元は Game）。
    /// エンジン側 Scene が Game.h に依存せず GPU パーティクルを駆動するために使う。
    /// </summary>
    /// <returns>GPUParticleManager（未初期化なら nullptr）</returns>
    GPUParticleManager* GetGPUParticleManager() const { return gpuParticleManager_; }

    /// <summary>ゲーム側の再生で使うカメラを設定する</summary>
    /// <param name="camera">シーンのカメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>Effect Editor のプレビュー用カメラを設定する。同じインスタンスをプレビュー RT にも描画する</summary>
    /// <param name="previewCamera">エディタのカメラ</param>
    void SetPreviewCamera(Camera* previewCamera) { previewCamera_ = previewCamera; }

    // ===== 定義の登録 =====
    /// <summary>
    /// 既にdefを構築済みの場合はそのまま登録（コピー）
    /// </summary>
    /// <param name="def">登録する定義（name が空なら登録しない。同名は上書き）</param>
    void RegisterDef(const EffectDef& def);

    /// <summary>
    /// JSONファイルから読み込んで登録。defのnameが空ならファイル名をフォールバック使用しない
    /// （EffectDef.name を使う）
    /// </summary>
    /// <param name="filePath">読む JSON ファイルのパス</param>
    /// <returns>登録できたら true。読み込み失敗・name が空なら false</returns>
    bool LoadDef(const std::string& filePath);

    /// <summary>
    /// ディレクトリ内の*.jsonをすべてLoadDef（再帰なし）
    /// </summary>
    /// <param name="dirPath">探すディレクトリ（無ければ何もしない）</param>
    void LoadAllDefsInDirectory(const std::string& dirPath);

    /// <summary>定義が登録済みか</summary>
    /// <param name="name">定義名</param>
    /// <returns>登録済みなら true</returns>
    bool HasDef(const std::string& name) const;
    /// <summary>登録済みの定義を探す</summary>
    /// <param name="name">定義名</param>
    /// <returns>定義。無ければ nullptr</returns>
    const EffectDef* FindDef(const std::string& name) const;

    /// <summary>
    /// 編集用にミュータブル参照を返す（Effect Editor 用）。無ければ nullptr。
    /// </summary>
    /// <param name="name">定義名</param>
    /// <returns>定義。無ければ nullptr</returns>
    EffectDef* FindDefMutable(const std::string& name);

    /// <summary>
    /// def を Resources/Json/Effects/ に保存し、メモリ上の defs_ にも同名で登録する。
    /// allowOverwrite=false の場合、既存名と衝突したら (1)(2)... を付与してユニーク化。
    /// 編集中のものを保存（同名で上書き）したい場合は allowOverwrite=true を渡す。
    /// 最終的に使用された名前を返す。
    /// </summary>
    /// <param name="def">保存する定義（name が空なら "Untitled"）</param>
    /// <param name="allowOverwrite">同名の定義を上書きするなら true</param>
    /// <returns>最終的に使用された名前。ファイル書き込みに失敗したら空文字</returns>
    std::string SaveDef(EffectDef def, bool allowOverwrite = false);

    /// <summary>
    /// 登録済み定義を削除（ファイル削除はしない）。
    /// </summary>
    /// <param name="name">削除する定義名</param>
    void UnregisterDef(const std::string& name);

    // ===== 再生 =====
    /// <summary>
    /// 指定名のエフェクトを worldPos に再生開始。defがなければ kInvalidEffectHandle。
    /// 戻り値は外部から Stop / SetPosition に使うハンドル。
    /// </summary>
    /// <param name="effectName">登録済みの定義名</param>
    /// <param name="worldPos">エフェクト中心のワールド座標</param>
    /// <returns>再生したインスタンスのハンドル。定義が無ければ kInvalidEffectHandle</returns>
    EffectHandle Play(const std::string& effectName, const Vector3& worldPos);

    /// <summary>
    /// 指定 def をそのまま使ってインスタンス再生（未保存の editBuffer プレビュー用）。
    /// EffectManager の defs_ には登録されない。
    /// </summary>
    /// <param name="def">再生する定義（インスタンスにコピーされる）</param>
    /// <param name="worldPos">エフェクト中心のワールド座標</param>
    /// <returns>再生したインスタンスのハンドル</returns>
    EffectHandle PlayWithDef(const EffectDef& def, const Vector3& worldPos);

    // ===== プレビュー専用（EffectEditor 用、ゲームと完全分離） =====
    /// <summary>
    /// 指定 def をプレビュー専用リストで再生する。activeInstances_（ゲーム）には
    /// 一切混ざらないため、シーン描画に漏れず、エディタが自前タイムラインで更新する。
    /// </summary>
    /// <param name="def">再生する定義（インスタンスにコピーされる）</param>
    /// <param name="worldPos">エフェクト中心のワールド座標</param>
    /// <returns>プレビューインスタンスのハンドル</returns>
    EffectHandle PlayPreview(const EffectDef& def, const Vector3& worldPos);

    /// <summary>
    /// プレビューインスタンスだけを更新する。エディタが unscaled な実 delta で
    /// 毎フレーム呼ぶ（シーンのタイムスケールに依存しない）。
    /// </summary>
    /// <param name="deltaTime">エディタのタイムラインで進める時間 [秒]（一時停止中は 0）</param>
    void UpdatePreview(float deltaTime);

    /// <summary>
    /// プレビューインスタンスだけを全停止する（ゲームの activeInstances_ には触れない）。
    /// </summary>
    void StopAllPreview();

    /// <summary>
    /// 全プレビューインスタンスへ編集中 def をライブ反映し、中心位置を追従させる。
    /// エディタが editBuffer の変化に応じて毎フレーム呼ぶ。
    /// </summary>
    /// <param name="def">編集中の定義</param>
    /// <param name="worldPos">エフェクト中心のワールド座標</param>
    void SyncPreview(const EffectDef& def, const Vector3& worldPos);
    /// <summary>プレビューのエフェクト全体の向きを設定する（エディタで動かす・ビームの向き）</summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetPreviewRotation(const Vector3& rotate);

    /// <summary>
    /// 全プレビューインスタンスを t=0 へ巻き戻す（「Restart」の実体）。
    /// </summary>
    void RewindPreview();

    /// <summary>
    /// プレビューを指定時刻へシークする。完全ランダムアクセスは不可なので
    /// 一旦 Rewind して固定ステップで targetTime まで早送り再シミュレートする。
    /// （パーティクル burst / 出現時ランダムは厳密再現できない点に注意）
    /// </summary>
    /// <param name="targetTime">シーク先の時刻 [秒]（負なら 0）</param>
    void SeekPreview(float targetTime);

    /// <summary>
    /// プレビューのループ表示。ON のとき、寿命を終えたプレビューを削除せず自動 Rewind して
    /// 編集中ずっと再生し続ける（既定 ON）。
    /// </summary>
    /// <param name="loop">ループ表示するなら true</param>
    void SetPreviewLoop(bool loop) { previewLoop_ = loop; }
    /// <summary>プレビューのループ表示が ON か</summary>
    /// <returns>ON なら true</returns>
    bool GetPreviewLoop() const { return previewLoop_; }

    /// <summary>
    /// 指定ハンドルのエフェクトが再生中なら停止要求を出す（次フレームで終了）。
    /// loop エフェクトを外から終わらせる場合に使う。
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル（プレビューのハンドルは対象外）</param>
    void Stop(EffectHandle handle);

    /// <summary>
    /// 指定ハンドルのエフェクト中心位置を更新（弾丸の追従用）。
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル（プレビューのハンドルは対象外）</param>
    /// <param name="pos">エフェクト中心のワールド座標</param>
    void SetPosition(EffectHandle handle, const Vector3& pos);

    /// <summary>
    /// 指定ハンドルのエフェクト全体の向きを更新（オイラー角・ラジアン）。
    /// 弾の進行方向にエフェクトを向ける等に使う。
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル（プレビューのハンドルは対象外）</param>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetRotation(EffectHandle handle, const Vector3& rotate);

    /// <summary>
    /// 出し終わりを要求する（弾が消えた・ビームを撃ち終えた）。Primitive / Light はすぐ消え、
    /// Ribbon の尾は古くなって、ビームは細くなって消えてから終わる（Stop と違って尾が残る）。
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル（プレビューのハンドルは対象外）</param>
    void Release(EffectHandle handle);

    /// <summary>
    /// ビームの長さを設定する（Ribbon の Beam と、ビームの先に付けた Primitive / Particle が使う）。
    /// 向きは SetRotation（エフェクトのローカル +Z がビームの前）。
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル（プレビューのハンドルは対象外）</param>
    /// <param name="length">長さ [m]（負で Ribbon の defaultLength に戻す）</param>
    void SetBeamLength(EffectHandle handle, float length);

    /// <summary>プレビューのエフェクトのビームの長さを設定する（エディタ用）</summary>
    /// <param name="length">長さ [m]（負で Ribbon の defaultLength に戻す）</param>
    void SetPreviewBeamLength(float length);

    /// <summary>
    /// 指定ハンドルが現在もアクティブなインスタンスを指しているか
    /// </summary>
    /// <param name="handle">Play / PlayWithDef が返したハンドル</param>
    /// <returns>ゲーム側の再生中インスタンスを指していれば true</returns>
    bool IsAlive(EffectHandle handle) const;

    /// <summary>
    /// 再生中のすべてのエフェクトを即終了させる
    /// </summary>
    void StopAll();

    // ===== 毎フレーム =====
    /// <summary>前フレームの破棄待ちを解放し、ゲーム側の全インスタンスを進め、終わったものを破棄待ちへ移す</summary>
    /// <param name="deltaTime">unscaled な実経過時間 [秒]</param>
    void Update(float deltaTime);
    /// <summary>ゲーム側の全インスタンスの Primitive をシーンへ描く</summary>
    void Draw();

    /// <summary>
    /// 水面の反射 RT へ GPU パーティクルと Primitive を描く（WaterReflection::Render から呼ぶ）。
    /// ブルーム・歪みのパスは描かない。パーティクルは前フレームに進めた状態をそのまま描く（シミュレーションしない）。
    /// </summary>
    /// <param name="mirroredView">水面で折り返したビュー行列</param>
    /// <param name="mirroredViewProjection">水面で折り返したビュープロジェクション行列</param>
    /// <param name="mirroredCameraPos">水面で折り返したカメラ位置</param>
    void DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos);

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
    /// <returns>あれば true</returns>
    bool HasActiveDistortionSource() const;

    /// <summary>
    /// useBloom な Primitive / GPU パーティクルを発光 RT に描画する。
    /// Game::Draw が Distortion Pass の後に呼ぶ想定。RTV/DSV/Viewport/Scissor は呼び出し側が設定済みであること。
    /// </summary>
    void DrawBloomPass();

    /// <summary>
    /// プレビュー版 Bloom パス。呼ぶ直前に DrawPreview() でプレビュー WVP / PerView が更新されている前提。
    /// </summary>
    void DrawBloomPassPreview();

    /// <summary>
    /// 発光させるものが1つでもあるか（無ければ Bloom パスと合成を丸ごとスキップできる）。
    /// preview=true ならエディタのプレビュー側を調べる。
    /// </summary>
    /// <param name="preview">true ならプレビュー側、false ならゲーム側を調べる</param>
    /// <returns>発光する GPU パーティクルのグループか、出現中の useBloom な Primitive があれば true</returns>
    bool HasActiveBloomSource(bool preview = false) const;

    /// <summary>
    /// プレビュー RT への描画。SetPreviewCamera で設定したカメラ視点で同じインスタンスを描画する。
    /// 呼び出し前にプレビュー RT を BeginRender 済みであること。
    /// </summary>
    void DrawPreview();

    /// <summary>ImGui デバッグUI（登録済みエフェクトと再生中インスタンス数を表示）</summary>
    void OnImGui();

    /// <summary>再生中インスタンス数（デバッグ用）</summary>
    /// <returns>ゲーム側の再生中インスタンス数（プレビューは含まない）</returns>
    size_t GetActiveInstanceCount() const { return activeInstances_.size(); }

    /// <summary>
    /// 先頭の再生中インスタンスを返す（Timeline表示用）。無ければnullptr。
    /// </summary>
    /// <returns>ゲーム側の先頭のインスタンス。無ければ nullptr</returns>
    EffectInstance* GetFirstActiveInstance() {
        for (auto& inst : activeInstances_) if (inst) return inst.get();
        return nullptr;
    }

    /// <summary>
    /// 指定ハンドルの再生中インスタンスを返す（EffectEditor のプレビュー Timeline 表示用）。無ければnullptr。
    /// </summary>
    /// <param name="handle">プレビューかゲーム側のハンドル（プレビューを先に探す）</param>
    /// <returns>インスタンス。無ければ nullptr</returns>
    EffectInstance* GetInstanceByHandle(EffectHandle handle) {
        auto pit = previewHandleToInstance_.find(handle);
        if (pit != previewHandleToInstance_.end()) return pit->second;
        auto it = handleToInstance_.find(handle);
        return (it != handleToInstance_.end()) ? it->second : nullptr;
    }

    /// <summary>登録済みエフェクト名一覧（デバッグ用）</summary>
    /// <returns>名前の昇順に並べた一覧</returns>
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

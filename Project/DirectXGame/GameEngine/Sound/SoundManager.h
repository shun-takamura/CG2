#pragma once
#include <xaudio2.h>
#include <x3daudio.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "ConvertString.h"
#include "Vector3.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

class Camera;

struct SoundData
{
    WAVEFORMATEX wfex;
    std::vector<BYTE> buffer;
};

// 3D音源1つ分のデータ
struct SoundEmitter3D
{
    IXAudio2SourceVoice* sourceVoice = nullptr;
    X3DAUDIO_EMITTER       emitter = {};
    X3DAUDIO_DSP_SETTINGS  dspSettings = {};
    std::vector<float>     matrixCoefficients; // パンニング用係数
    std::vector<float> channelAzimuths; // ステレオ時のチャンネル配置角度
    std::string            soundName;
};

/// <summary>
/// 効果音・BGM の読み込みと再生・停止・音量を管理する。
/// シングルトンにする理由：XAudio2 のエンジンとマスターボイスはプロセスに 1 つだけ作るため。
/// </summary>
class SoundManager
{
public:
    /// <summary>唯一のインスタンス</summary>
    /// <returns>インスタンス</returns>
    static SoundManager* GetInstance();

    /// <summary>Media Foundation・XAudio2（マスタリングボイス）・X3DAudio を初期化し、リスナーを原点で +Z 向きにする</summary>
    void Initialize();
    /// <summary>2D・3D の再生中の音をすべて止めて破棄し、読み込んだ音を捨てて Media Foundation を終える</summary>
    void Finalize();
    /// <summary>再生の終わった 3D の音を破棄し、鳴っている 3D の音の定位を更新する（毎フレーム呼ぶ）</summary>
    void Update(); // 毎フレーム呼ぶ（終了検知・3DDSP更新）

    /// <summary>音声ファイルを読み込んで名前で登録する（wav, mp3, aac などまとめて読み込める。同じ名前が登録済みなら何もしない）</summary>
    /// <param name="name">登録する名前</param>
    /// <param name="filename">ファイルのパス</param>
    void LoadFile(const std::string& name, const std::string& filename);
    /// <summary>2D の再生を止め、登録を消す</summary>
    /// <param name="name">登録した名前</param>
    void Unload(const std::string& name);

    /// <summary>2D で 1 回鳴らす（BGM・UI 用。名前が無ければ何もしない）</summary>
    /// <param name="name">登録した名前</param>
    /// <param name="volume">この再生だけに掛かる倍率（1.0 = 素材のまま）</param>
    void Play2DSound(const std::string& name, float volume = 1.0f);
    /// <summary>
    /// 2D でバッファ全体を無限ループ再生する（BGM 用）。止めるときは Stop2DSound(name)。
    /// 同じ name で呼び直すと前の再生を止めて鳴らし直す
    /// </summary>
    /// <param name="name">登録した名前</param>
    /// <param name="volume">この再生だけに掛かる倍率（1.0 = 素材のまま）</param>
    void Play2DSoundLooped(const std::string& name, float volume = 1.0f);
    /// <summary>2D の再生を止めて破棄する（再生していなければ何もしない）</summary>
    /// <param name="name">登録した名前</param>
    void Stop2DSound(const std::string& name);
    /// <summary>再生中の 2D 音の音量を変える（フェードは呼び出し側で毎フレーム呼ぶ）。再生していなければ何もしない</summary>
    /// <param name="name">登録した名前</param>
    /// <param name="volume">倍率（1.0 = 素材のまま）</param>
    void Set2DSoundVolume(const std::string& name, float volume);

    /// <summary>3D の位置で鳴らす（SE 用）。再生が終わると Update で自動的に破棄される</summary>
    /// <param name="name">登録した名前</param>
    /// <param name="position">音源の位置（ワールド座標）</param>
    /// <param name="velocity">音源の速度（ドップラー用）</param>
    /// <param name="distanceScale">聴こえる距離のスケール</param>
    /// <returns>止める・動かすためのハンドル（名前が無ければ 0＝無効）</returns>
    uint32_t Play3DSound(
        const std::string& name,
        const Vector3& position,
        const Vector3& velocity = { 0.0f, 0.0f, 0.0f },
        float distanceScale = 20.0f);   // 聴こえる距離のスケール

    /// <summary>3D の音を止めて破棄する（無効なハンドルは無視）</summary>
    /// <param name="handle">Play3DSound の戻り値</param>
    void Stop3DSound(uint32_t handle);

    /// <summary>3D の音源の位置と速度を変える（動くオブジェクトは毎フレーム呼ぶ。無効なハンドルは無視）</summary>
    /// <param name="handle">Play3DSound の戻り値</param>
    /// <param name="position">音源の位置（ワールド座標）</param>
    /// <param name="velocity">音源の速度</param>
    void UpdateEmitter(uint32_t handle, const Vector3& position, const Vector3& velocity);

    /// <summary>カメラの位置と向き（前・上）をリスナーに反映する（毎フレーム呼ぶ。nullptr なら何もしない）</summary>
    /// <param name="camera">聞き手にするカメラ</param>
    void UpdateListener(const Camera* camera);

private:
    SoundManager() = default;
    ~SoundManager() = default;
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    /// <summary>リスナーとの位置関係から 3D の音の定位（パン・音量・ドップラー）を計算してボイスに設定する</summary>
    /// <param name="emitter3d">更新する 3D の音</param>
    void Apply3DDSP(SoundEmitter3D& emitter3d);

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    X3DAUDIO_HANDLE   x3dAudio_;
    X3DAUDIO_LISTENER listener_ = {};
    UINT32            outputChannels_ = 2;

    std::unordered_map<std::string, SoundData>              soundDatas_;
    std::unordered_map<std::string, IXAudio2SourceVoice*>   sourceVoices2D_;
    std::unordered_map<uint32_t, SoundEmitter3D>         emitters3D_;
    uint32_t nextHandle_ = 1;
};
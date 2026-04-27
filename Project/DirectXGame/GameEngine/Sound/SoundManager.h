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

class SoundManager
{
public:
    static SoundManager* GetInstance();

    void Initialize();
    void Finalize();
    void Update(); // 毎フレーム呼ぶ（終了検知・3DDSP更新）

    // wav, mp3, aac などまとめて読み込める
    void LoadFile(const std::string& name, const std::string& filename);
    void Unload(const std::string& name);

    // 2D再生（BGM用）
    void Play2DSound(const std::string& name);
    void Stop2DSound(const std::string& name);

    // 3D再生（SE用）※ ハンドルを返す、0は無効
    uint32_t Play3DSound(
        const std::string& name,
        const Vector3& position,
        const Vector3& velocity = { 0.0f, 0.0f, 0.0f },
        float distanceScale = 20.0f);   // 聴こえる距離のスケール

    void Stop3DSound(uint32_t handle);

    // 動くオブジェクトは毎フレーム呼ぶ
    void UpdateEmitter(uint32_t handle, const Vector3& position, const Vector3& velocity);

    // カメラ情報をリスナーに反映する（毎フレーム呼ぶ）
    void UpdateListener(const Camera* camera);

private:
    SoundManager() = default;
    ~SoundManager() = default;
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

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
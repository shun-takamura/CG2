#pragma once
#include <xaudio2.h>
#include <wrl.h>
#include <fstream>
#include <unordered_map>
#include <string>

// 音声データの構造体（内部で使用）
struct SoundData
{
    WAVEFORMATEX wfex;
    BYTE* pBuffer;
    unsigned int bufferSize;
};

class SoundManager
{
public:
    // シングルトン
    static SoundManager* GetInstance();

    // 初期化・終了
    void Initialize();
    void Finalize();

    // 音声データの読み込み（名前をつけて管理）
    void LoadWave(const std::string& name, const std::string& filename);

    // 音声データの解放
    void Unload(const std::string& name);

    // 再生
    void Play(const std::string& name);

    // 停止（必要なら）
    //void Stop(const std::string& name);

private:
    SoundManager() = default;
    ~SoundManager() = default;
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    // WAVファイル読み込み（内部用）
    SoundData LoadWaveFile(const std::string& filename);

    // XAudio2
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    // 音声データをキーで管理
    std::unordered_map<std::string, SoundData> soundDatas_;
};
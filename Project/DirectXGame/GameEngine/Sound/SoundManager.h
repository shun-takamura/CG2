#pragma once
#include <xaudio2.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <unordered_map>
#include <string>
#include <vector>
#include"ConvertString.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

struct SoundData
{
    WAVEFORMATEX wfex;
    std::vector<BYTE> buffer;
};

class SoundManager
{
public:
    static SoundManager* GetInstance();

    void Initialize();
    void Finalize();

    // wav, mp3, aac などまとめて読み込める
    void LoadFile(const std::string& name, const std::string& filename);
    void Unload(const std::string& name);

    void Play(const std::string& name);
    void Stop(const std::string& name);

private:
    SoundManager() = default;
    ~SoundManager() = default;
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    std::unordered_map<std::string, SoundData> soundDatas_;
    std::unordered_map<std::string, IXAudio2SourceVoice*> sourceVoices_;
};
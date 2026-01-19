// SoundManager.cpp
#include "SoundManager.h"
#include <cassert>
#include <cstring>
#include <vector>

// チャンクヘッダ
struct ChunkHeader
{
    char id[4];
    int32_t size;
};

// RIFFヘッダチャンク
struct RiffHeader
{
    ChunkHeader chunk;
    char type[4];
};

// FMTチャンク
struct FormatChunk
{
    ChunkHeader chunk;
    WAVEFORMATEX fmt;
};

SoundManager* SoundManager::GetInstance()
{
    static SoundManager instance;
    return &instance;
}

void SoundManager::Initialize()
{
    HRESULT hr;

    // XAudio2エンジンのインスタンスを生成
    hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    // マスターボイスを生成
    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(hr));
}

void SoundManager::Finalize()
{
    // XAudio2解放
    xAudio2_.Reset();

    // 全ての音声データを解放
    for (auto& pair : soundDatas_)
    {
        delete[] pair.second.pBuffer;
    }
    soundDatas_.clear();
}

void SoundManager::LoadWave(const std::string& name, const std::string& filename)
{
    // 既に読み込み済みならスキップ
    if (soundDatas_.find(name) != soundDatas_.end())
    {
        return;
    }

    soundDatas_[name] = LoadWaveFile(filename);
}

void SoundManager::Unload(const std::string& name)
{
    auto it = soundDatas_.find(name);
    if (it != soundDatas_.end())
    {
        delete[] it->second.pBuffer;
        soundDatas_.erase(it);
    }
}

void SoundManager::Play(const std::string& name)
{
    auto it = soundDatas_.find(name);
    if (it == soundDatas_.end())
    {
        return; // 見つからない
    }

    SoundData& soundData = it->second;

    // SourceVoiceの生成
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(hr));

    // 再生する波形データの設定
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.pBuffer;
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    // 再生
    hr = pSourceVoice->SubmitSourceBuffer(&buf);
    hr = pSourceVoice->Start();
}

SoundData SoundManager::LoadWaveFile(const std::string& filename)
{
    // ファイルを開く
    std::ifstream file;
    file.open(filename, std::ios_base::binary);
    assert(file.is_open());

    // RIFFヘッダの読み込み
    RiffHeader riff;
    file.read((char*)&riff, sizeof(riff));

    if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
    {
        assert(0);
    }
    if (strncmp(riff.type, "WAVE", 4) != 0)
    {
        assert(0);
    }

    // チャンクヘッダ読み込み
    ChunkHeader chunk = {};
    file.read((char*)&chunk, sizeof(ChunkHeader));

    if (strncmp(chunk.id, "fmt ", 4) != 0)
    {
        assert(0);
    }

    FormatChunk format = {};
    format.chunk = chunk;

    const size_t kFmtLimitSize = sizeof(format.fmt);
    std::vector<char> buffer(chunk.size);
    file.read(buffer.data(), chunk.size);
    std::memcpy(&format.fmt, buffer.data(), kFmtLimitSize);

    // Dataチャンクの読み込み
    ChunkHeader data;
    file.read((char*)&data, sizeof(data));

    // JUNKチャンクを検知した場合
    if (strncmp(data.id, "JUNK", 4) == 0)
    {
        file.seekg(data.size, std::ios_base::cur);
        file.read((char*)&data, sizeof(data));
    }

    if (strncmp(data.id, "data", 4) != 0)
    {
        assert(0);
    }

    // 波形データの読み込み
    char* pBuffer = new char[data.size];
    file.read(pBuffer, data.size);

    file.close();

    // SoundDataを返す
    SoundData soundData = {};
    soundData.wfex = format.fmt;
    soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
    soundData.bufferSize = data.size;

    return soundData;
}
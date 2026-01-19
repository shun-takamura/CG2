#include "SoundManager.h"
#include <cassert>

SoundManager* SoundManager::GetInstance()
{
    static SoundManager instance;
    return &instance;
}

void SoundManager::Initialize()
{
    HRESULT result;

    // Media Foundation の初期化
    result = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    assert(SUCCEEDED(result));

    // XAudio2 の初期化
    result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(result));

    result = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(result));
}

void SoundManager::Finalize()
{
    // 全ての再生中ボイスを停止・破棄
    for (auto& pair : sourceVoices_)
    {
        pair.second->Stop();
        pair.second->DestroyVoice();
    }
    sourceVoices_.clear();

    // XAudio2 解放
    xAudio2_.Reset();

    // 音声データ解放
    soundDatas_.clear();

    // Media Foundation の終了
    MFShutdown();
}

void SoundManager::LoadFile(const std::string& name, const std::string& filename)
{
    // 既に読み込み済みならスキップ
    if (soundDatas_.find(name) != soundDatas_.end())
    {
        return;
    }

    HRESULT result;

    // フルパスをワイド文字列に変換
    std::wstring filePathW = ConvertString(filename);

    // SourceReader 作成
    Microsoft::WRL::ComPtr<IMFSourceReader> pReader;
    result = MFCreateSourceReaderFromURL(filePathW.c_str(), nullptr, &pReader);
    assert(SUCCEEDED(result));

    // PCM形式にフォーマット指定する
    Microsoft::WRL::ComPtr<IMFMediaType> pPCMType;
    MFCreateMediaType(&pPCMType);
    pPCMType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pPCMType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    result = pReader->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPCMType.Get());
    assert(SUCCEEDED(result));

    // 実際にセットされたメディアタイプを取得する
    Microsoft::WRL::ComPtr<IMFMediaType> pOutType;
    pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pOutType);

    // Waveフォーマットを取得する
    WAVEFORMATEX* waveFormat = nullptr;
    MFCreateWaveFormatExFromMFMediaType(pOutType.Get(), &waveFormat, nullptr);

    // コンテナに格納する音声データ
    SoundData soundData = {};
    soundData.wfex = *waveFormat;

    // 生成したWaveフォーマットを解放
    CoTaskMemFree(waveFormat);

    // PCMデータのバッファを構築
    while (true)
    {
        Microsoft::WRL::ComPtr<IMFSample> pSample;
        DWORD streamIndex = 0, flags = 0;
        LONGLONG llTimeStamp = 0;

        // サンプルを読み込む
        result = pReader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
            &streamIndex, &flags, &llTimeStamp, &pSample);

        // ストリームの末尾に達したら抜ける
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            break;
        }

        if (pSample)
        {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
            // サンプルに含まれるサウンドデータのバッファを一繋ぎにして取得
            pSample->ConvertToContiguousBuffer(&pBuffer);

            BYTE* pData = nullptr;
            DWORD maxLength = 0, currentLength = 0;

            // バッファ読み込み用にロック
            pBuffer->Lock(&pData, &maxLength, &currentLength);

            // バッファの末尾にデータを追加
            soundData.buffer.insert(soundData.buffer.end(), pData, pData + currentLength);

            pBuffer->Unlock();
        }
    }

    soundDatas_[name] = std::move(soundData);
}

void SoundManager::Unload(const std::string& name)
{
    // 再生中なら停止
    Stop(name);

    auto it = soundDatas_.find(name);
    if (it != soundDatas_.end())
    {
        soundDatas_.erase(it);
    }
}

void SoundManager::Play(const std::string& name)
{
    auto it = soundDatas_.find(name);
    if (it == soundDatas_.end())
    {
        return;
    }

    // 既に再生中なら停止
    Stop(name);

    SoundData& soundData = it->second;

    IXAudio2SourceVoice* pSourceVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(result));

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;

    result = pSourceVoice->SubmitSourceBuffer(&buf);
    result = pSourceVoice->Start();

    sourceVoices_[name] = pSourceVoice;
}

void SoundManager::Stop(const std::string& name)
{
    auto it = sourceVoices_.find(name);
    if (it == sourceVoices_.end())
    {
        return;
    }

    it->second->Stop();
    it->second->DestroyVoice();
    sourceVoices_.erase(it);
}
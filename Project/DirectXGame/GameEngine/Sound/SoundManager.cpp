#include "SoundManager.h"
#include "Camera.h"
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

    // マスタリングボイスの出力チャンネル数を取得（ステレオなら2）
    XAUDIO2_VOICE_DETAILS masterDetails;
    masterVoice_->GetVoiceDetails(&masterDetails);
    outputChannels_ = masterDetails.InputChannels;

    // X3DAudio の初期化（左手座標系）
    DWORD channelMask;
    masterVoice_->GetChannelMask(&channelMask);
    memset(x3dAudio_, 0, sizeof(x3dAudio_));
    X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, x3dAudio_);

    // リスナーの初期値（Z方向を向いてY方向が上）
    listener_.OrientFront = { 0.0f, 0.0f, 1.0f };
    listener_.OrientTop = { 0.0f, 1.0f, 0.0f };
    listener_.Position = { 0.0f, 0.0f, 0.0f };
    listener_.Velocity = { 0.0f, 0.0f, 0.0f };
    listener_.pCone = nullptr; // 全方向に聴こえる
}

void SoundManager::Finalize()
{
    // 全ての3D音源を停止・破棄
    for (auto& pair : emitters3D_)
    {
        pair.second.sourceVoice->Stop();
        pair.second.sourceVoice->DestroyVoice();
    }
    emitters3D_.clear();

    // 全ての2D音源を停止・破棄
    for (auto& pair : sourceVoices2D_)
    {
        pair.second->Stop();
        pair.second->DestroyVoice();
    }
    sourceVoices2D_.clear();

    xAudio2_.Reset();
    soundDatas_.clear();
    MFShutdown();
}

void SoundManager::Update()
{
    for (auto it = emitters3D_.begin(); it != emitters3D_.end(); )
    {
        XAUDIO2_VOICE_STATE state;
        it->second.sourceVoice->GetState(&state);

        if (state.BuffersQueued == 0)
        {
            // 再生が終わったので自動破棄
            it->second.sourceVoice->DestroyVoice();
            it = emitters3D_.erase(it);
        } else
        {
            // 3DサウンドのDSP設定を毎フレーム更新
            Apply3DDSP(it->second);
            ++it;
        }
    }
}

void SoundManager::LoadFile(const std::string& name, const std::string& filename)
{
    if (soundDatas_.find(name) != soundDatas_.end()) { return; }

    HRESULT result;
    std::wstring filePathW = ConvertString(filename);

    Microsoft::WRL::ComPtr<IMFSourceReader> pReader;
    result = MFCreateSourceReaderFromURL(filePathW.c_str(), nullptr, &pReader);
    assert(SUCCEEDED(result));

    Microsoft::WRL::ComPtr<IMFMediaType> pPCMType;
    MFCreateMediaType(&pPCMType);
    pPCMType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pPCMType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    result = pReader->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPCMType.Get());
    assert(SUCCEEDED(result));

    Microsoft::WRL::ComPtr<IMFMediaType> pOutType;
    pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pOutType);

    WAVEFORMATEX* waveFormat = nullptr;
    MFCreateWaveFormatExFromMFMediaType(pOutType.Get(), &waveFormat, nullptr);

    SoundData soundData = {};
    soundData.wfex = *waveFormat;
    CoTaskMemFree(waveFormat);

    while (true)
    {
        Microsoft::WRL::ComPtr<IMFSample> pSample;
        DWORD streamIndex = 0, flags = 0;
        LONGLONG llTimeStamp = 0;

        result = pReader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
            &streamIndex, &flags, &llTimeStamp, &pSample);

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { break; }

        if (pSample)
        {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
            pSample->ConvertToContiguousBuffer(&pBuffer);

            BYTE* pData = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            pBuffer->Lock(&pData, &maxLength, &currentLength);
            soundData.buffer.insert(soundData.buffer.end(), pData, pData + currentLength);
            pBuffer->Unlock();
        }
    }

    soundDatas_[name] = std::move(soundData);
}

void SoundManager::Unload(const std::string& name)
{
    Stop2DSound(name);

    auto it = soundDatas_.find(name);
    if (it != soundDatas_.end()) { soundDatas_.erase(it); }
}

void SoundManager::Play2DSound(const std::string& name)
{
    auto it = soundDatas_.find(name);
    if (it == soundDatas_.end()) { return; }

    Stop2DSound(name);

    SoundData& soundData = it->second;

    IXAudio2SourceVoice* pSourceVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(result));

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;

    pSourceVoice->SubmitSourceBuffer(&buf);
    pSourceVoice->Start();

    sourceVoices2D_[name] = pSourceVoice;
}

void SoundManager::Stop2DSound(const std::string& name)
{
    auto it = sourceVoices2D_.find(name);
    if (it == sourceVoices2D_.end()) { return; }

    it->second->Stop();
    it->second->DestroyVoice();
    sourceVoices2D_.erase(it);
}

uint32_t SoundManager::Play3DSound(
    const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    float distanceScale)
{
    auto it = soundDatas_.find(name);
    if (it == soundDatas_.end()) { return 0; }

    SoundData& soundData = it->second;

    // XAUDIO2_VOICE_USEFILTER: 距離による音の篭り(LPF)を使うために必要
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(
        &pSourceVoice, &soundData.wfex, XAUDIO2_VOICE_USEFILTER);
    assert(SUCCEEDED(result));

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;

    pSourceVoice->SubmitSourceBuffer(&buf);
    pSourceVoice->Start();

    SoundEmitter3D emitter3d;
    emitter3d.sourceVoice = pSourceVoice;
    emitter3d.soundName = name;

    UINT32 srcChannels = soundData.wfex.nChannels; // ファイルの実際のチャンネル数を使う

    // matrixCoefficients のサイズを「ソースch数 × 出力ch数」に合わせる
    emitter3d.matrixCoefficients.resize(srcChannels * outputChannels_, 0.0f);

    emitter3d.dspSettings = {};
    emitter3d.dspSettings.SrcChannelCount = srcChannels;
    emitter3d.dspSettings.DstChannelCount = outputChannels_;
    emitter3d.dspSettings.pMatrixCoefficients = emitter3d.matrixCoefficients.data();

    // エミッタの設定
    emitter3d.emitter = {};
    emitter3d.emitter.pCone = nullptr;
    emitter3d.emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
    emitter3d.emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
    emitter3d.emitter.Position = { position.x, position.y, position.z };
    emitter3d.emitter.Velocity = { velocity.x, velocity.y, velocity.z };
    emitter3d.emitter.ChannelCount = srcChannels;
    emitter3d.emitter.ChannelRadius = (srcChannels > 1) ? 1.0f : 0.0f;
    emitter3d.emitter.CurveDistanceScaler = distanceScale;
    emitter3d.emitter.DopplerScaler = 1.0f;

    // ステレオファイルの場合は左右チャンネルの配置角度が必要
    if (srcChannels == 2)
    {
        emitter3d.channelAzimuths = { 3.0f * X3DAUDIO_PI / 2.0f, X3DAUDIO_PI / 2.0f };
        emitter3d.emitter.pChannelAzimuths = emitter3d.channelAzimuths.data();
    }

    uint32_t handle = nextHandle_++;
    emitters3D_[handle] = std::move(emitter3d);

    // 初回のDSP計算を即反映
    Apply3DDSP(emitters3D_[handle]);

    return handle;
}

void SoundManager::Stop3DSound(uint32_t handle)
{
    auto it = emitters3D_.find(handle);
    if (it == emitters3D_.end()) { return; }

    it->second.sourceVoice->Stop();
    it->second.sourceVoice->DestroyVoice();
    emitters3D_.erase(it);
}

void SoundManager::UpdateEmitter(uint32_t handle, const Vector3& position, const Vector3& velocity)
{
    auto it = emitters3D_.find(handle);
    if (it == emitters3D_.end()) { return; }

    it->second.emitter.Position = { position.x, position.y, position.z };
    it->second.emitter.Velocity = { velocity.x,  velocity.y,  velocity.z };
}

void SoundManager::UpdateListener(const Camera* camera)
{
    if (!camera) { return; }

    Vector3 pos = camera->GetTranslate();
    Vector3 forward = camera->GetForward();
    Vector3 up = camera->GetUp();

    listener_.Position = { pos.x,     pos.y,     pos.z };
    listener_.OrientFront = { forward.x, forward.y, forward.z };
    listener_.OrientTop = { up.x,      up.y,      up.z };
    listener_.Velocity = { 0.0f, 0.0f, 0.0f };
}

void SoundManager::Apply3DDSP(SoundEmitter3D& emitter3d)
{
    // map内で再配置されても壊れないようにポインタを都度更新
    emitter3d.dspSettings.pMatrixCoefficients = emitter3d.matrixCoefficients.data();

    // moveによってポインタがずれないよう都度更新
    if (!emitter3d.channelAzimuths.empty())
    {
        emitter3d.emitter.pChannelAzimuths = emitter3d.channelAzimuths.data();
    }

    X3DAudioCalculate(
        x3dAudio_, &listener_, &emitter3d.emitter,
        X3DAUDIO_CALCULATE_MATRIX |      // パンニング（左右の定位）
        X3DAUDIO_CALCULATE_DOPPLER |     // ドップラー効果（移動による音程変化）
        X3DAUDIO_CALCULATE_LPF_DIRECT,  // 距離による高音の篭り
        &emitter3d.dspSettings);

    // デバッグ出力（確認後に消す）
    char buf[256];
    sprintf_s(buf, "Matrix[0]=%.3f Matrix[1]=%.3f Doppler=%.3f LPF=%.3f\n",
        emitter3d.matrixCoefficients[0],
        emitter3d.matrixCoefficients.size() > 1 ? emitter3d.matrixCoefficients[1] : 0.0f,
        emitter3d.dspSettings.DopplerFactor,
        emitter3d.dspSettings.LPFDirectCoefficient);
    OutputDebugStringA(buf);

    // パンニングを適用
    emitter3d.sourceVoice->SetOutputMatrix(
        masterVoice_,
        emitter3d.dspSettings.SrcChannelCount,
        outputChannels_,
        emitter3d.matrixCoefficients.data());

    // ドップラー効果を適用
    emitter3d.sourceVoice->SetFrequencyRatio(emitter3d.dspSettings.DopplerFactor);

    // LPFフィルタを適用（遠くなるほど高音が消えて篭った音になる）
    XAUDIO2_FILTER_PARAMETERS filterParams = {};
    filterParams.Type = LowPassFilter;
    filterParams.Frequency = emitter3d.dspSettings.LPFDirectCoefficient;
    filterParams.OneOverQ = 1.0f;
    emitter3d.sourceVoice->SetFilterParameters(&filterParams);
}
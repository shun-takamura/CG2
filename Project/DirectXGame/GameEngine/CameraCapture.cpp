// CameraCapture.cpp
#include "CameraCapture.h"
#include <imgui.h>
#include <cassert>

CameraCapture* CameraCapture::GetInstance()
{
    static CameraCapture instance;
    return &instance;
}

void CameraCapture::Initialize()
{
    EnumerateDevices();
}

void CameraCapture::Finalize()
{
    CloseCamera();
    devices_.clear();
}

void CameraCapture::UpdateTexture()
{
    if (!isOpened_)
    {
        return;
    }

    // フレームを取得
    if (!CaptureFrame())
    {
        OutputDebugStringA("CameraCapture: CaptureFrame failed\n");
        return;
    }

    // フレームデータが空なら何もしない
    if (frameBuffer_.empty() || frameWidth_ == 0 || frameHeight_ == 0)
    {
        OutputDebugStringA("CameraCapture: Invalid frame data\n");
        return;
    }

    // テクスチャが未作成なら作成
    if (!textureCreated_)
    {
        char buf[256];
        sprintf_s(buf, "CameraCapture: Creating texture %u x %u\n", frameWidth_, frameHeight_);
        OutputDebugStringA(buf);

        TextureManager::GetInstance()->CreateDynamicTexture(
            kCameraTextureName_,
            frameWidth_,
            frameHeight_,
            DXGI_FORMAT_B8G8R8A8_UNORM
        );
        textureCreated_ = true;

        OutputDebugStringA("CameraCapture: Texture created\n");
    }

    // テクスチャを更新
    if (textureCreated_)
    {
        TextureManager::GetInstance()->UpdateDynamicTexture(
            kCameraTextureName_,
            frameBuffer_.data(),
            frameBuffer_.size()
        );
    }
}

void CameraCapture::EnumerateDevices()
{
    devices_.clear();

    HRESULT result;

    Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
    result = MFCreateAttributes(&pAttributes, 1);
    assert(SUCCEEDED(result));

    result = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    assert(SUCCEEDED(result));

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    result = MFEnumDeviceSources(pAttributes.Get(), &ppDevices, &deviceCount);

    if (FAILED(result) || deviceCount == 0)
    {
        return;
    }

    for (UINT32 i = 0; i < deviceCount; i++)
    {
        CameraDeviceInfo info;
        info.activate = ppDevices[i];

        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;
        result = ppDevices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendlyName,
            &nameLength
        );
        if (SUCCEEDED(result) && friendlyName)
        {
            info.name = friendlyName;
            CoTaskMemFree(friendlyName);
        }

        WCHAR* symbolicLink = nullptr;
        UINT32 linkLength = 0;
        result = ppDevices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &symbolicLink,
            &linkLength
        );
        if (SUCCEEDED(result) && symbolicLink)
        {
            info.symbolicLink = symbolicLink;
            CoTaskMemFree(symbolicLink);
        }

        devices_.push_back(std::move(info));
    }

    CoTaskMemFree(ppDevices);
}

bool CameraCapture::OpenCamera(uint32_t deviceIndex)
{
    if (deviceIndex >= devices_.size())
    {
        OutputDebugStringA("CameraCapture: Invalid device index\n");
        return false;
    }

    // 既に開いていたら閉じる
    CloseCamera();

    HRESULT result;

    // ========================================
    // デバイスを再列挙して新しいActivateを取得
    // ========================================
    Microsoft::WRL::ComPtr<IMFAttributes> pEnumAttributes;
    result = MFCreateAttributes(&pEnumAttributes, 1);
    if (FAILED(result))
    {
        OutputDebugStringA("CameraCapture: MFCreateAttributes for enum failed\n");
        return false;
    }

    result = pEnumAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(result))
    {
        OutputDebugStringA("CameraCapture: SetGUID for enum failed\n");
        return false;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    result = MFEnumDeviceSources(pEnumAttributes.Get(), &ppDevices, &deviceCount);
    if (FAILED(result) || deviceIndex >= deviceCount)
    {
        OutputDebugStringA("CameraCapture: MFEnumDeviceSources failed or index out of range\n");
        if (ppDevices)
        {
            for (UINT32 i = 0; i < deviceCount; i++)
            {
                ppDevices[i]->Release();
            }
            CoTaskMemFree(ppDevices);
        }
        return false;
    }

    // 新しいActivateからMediaSourceを作成
    result = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&mediaSource_));

    // 全てのActivateを解放
    for (UINT32 i = 0; i < deviceCount; i++)
    {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(result))
    {
        char buf[256];
        sprintf_s(buf, "CameraCapture: ActivateObject failed (0x%08X)\n", result);
        OutputDebugStringA(buf);
        return false;
    }
    OutputDebugStringA("CameraCapture: ActivateObject OK\n");

    // SourceReaderを作成
    result = MFCreateSourceReaderFromMediaSource(
        mediaSource_.Get(),
        nullptr,
        &sourceReader_
    );
    if (FAILED(result))
    {
        char buf[256];
        sprintf_s(buf, "CameraCapture: MFCreateSourceReaderFromMediaSource failed (0x%08X)\n", result);
        OutputDebugStringA(buf);
        CloseCamera();
        return false;
    }
    OutputDebugStringA("CameraCapture: SourceReader created OK\n");

    // 出力フォーマットをRGB32に設定
    Microsoft::WRL::ComPtr<IMFMediaType> pType;
    result = MFCreateMediaType(&pType);
    if (FAILED(result))
    {
        OutputDebugStringA("CameraCapture: MFCreateMediaType failed\n");
        CloseCamera();
        return false;
    }

    pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

    result = sourceReader_->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        nullptr,
        pType.Get()
    );
    if (FAILED(result))
    {
        char buf[256];
        sprintf_s(buf, "CameraCapture: SetCurrentMediaType RGB32 failed (0x%08X)\n", result);
        OutputDebugStringA(buf);

        // RGB32がダメならNV12を試す
        pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        result = sourceReader_->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            nullptr,
            pType.Get()
        );
        if (FAILED(result))
        {
            sprintf_s(buf, "CameraCapture: SetCurrentMediaType NV12 also failed (0x%08X)\n", result);
            OutputDebugStringA(buf);
            CloseCamera();
            return false;
        }
        OutputDebugStringA("CameraCapture: Using NV12 format instead\n");
    } else
    {
        OutputDebugStringA("CameraCapture: Using RGB32 format\n");
    }

    // 解像度を取得
    Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
    result = sourceReader_->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pCurrentType
    );
    if (SUCCEEDED(result))
    {
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        frameWidth_ = width;
        frameHeight_ = height;

        char buf[256];
        sprintf_s(buf, "CameraCapture: Resolution %u x %u\n", width, height);
        OutputDebugStringA(buf);
    }

    isOpened_ = true;
    OutputDebugStringA("CameraCapture: Camera opened successfully!\n");
    return true;
}

void CameraCapture::CloseCamera()
{
    // SourceReaderを先に解放
    if (sourceReader_)
    {
        sourceReader_.Reset();
    }

    // MediaSourceをシャットダウンして解放
    if (mediaSource_)
    {
        mediaSource_->Shutdown();
        mediaSource_.Reset();
    }

    frameBuffer_.clear();
    frameWidth_ = 0;
    frameHeight_ = 0;
    isOpened_ = false;

    textureCreated_ = false;

    OutputDebugStringA("CameraCapture: Camera closed\n");
}

bool CameraCapture::CaptureFrame()
{
    if (!isOpened_ || !sourceReader_)
    {
        return false;
    }

    HRESULT result;
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    Microsoft::WRL::ComPtr<IMFSample> pSample;

    result = sourceReader_->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &timestamp,
        &pSample
    );

    if (FAILED(result) || !pSample)
    {
        return false;
    }

    // バッファを取得
    Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
    result = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(result))
    {
        return false;
    }

    BYTE* pData = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;

    result = pBuffer->Lock(&pData, &maxLength, &currentLength);
    if (FAILED(result))
    {
        return false;
    }

    // フレームデータをコピー
    frameBuffer_.resize(currentLength);
    memcpy(frameBuffer_.data(), pData, currentLength);

    pBuffer->Unlock();

    return true;
}

void CameraCapture::LogDevicesToImGui()
{
    ImGui::Text("=== Camera Devices ===");

    if (devices_.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No camera devices found");
    } else
    {
        ImGui::Text("Found %zu device(s):", devices_.size());
        ImGui::Separator();

        for (size_t i = 0; i < devices_.size(); i++)
        {
            const auto& device = devices_[i];
            std::string nameUtf8 = ConvertString(device.name);

            // カメラを開くボタン
            std::string buttonLabel = "Open##" + std::to_string(i);
            if (ImGui::Button(buttonLabel.c_str()))
            {
                OpenCamera(static_cast<uint32_t>(i));
            }
            ImGui::SameLine();
            ImGui::Text("[%zu] %s", i, nameUtf8.c_str());
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Refresh Devices"))
    {
        EnumerateDevices();
    }

    // カメラ状態表示
    ImGui::Separator();
    ImGui::Text("=== Camera Status ===");

    if (isOpened_)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Camera: OPENED");
        ImGui::Text("Resolution: %u x %u", frameWidth_, frameHeight_);

        if (ImGui::Button("Close Camera"))
        {
            CloseCamera();
        }

        // テスト用: フレーム取得ボタン
        if (ImGui::Button("Capture Frame"))
        {
            if (CaptureFrame())
            {
                ImGui::Text("Frame captured: %zu bytes", frameBuffer_.size());
            }
        }

        if (!frameBuffer_.empty())
        {
            ImGui::Text("Buffer size: %zu bytes", frameBuffer_.size());
        }
    } else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Camera: CLOSED");
    }
}
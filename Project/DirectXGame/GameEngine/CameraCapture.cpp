#include "CameraCapture.h"
#include "QRCodeReader.h"
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

    if (!CaptureFrame())
    {
        return;
    }

    if (frameBuffer_.empty() || frameWidth_ == 0 || frameHeight_ == 0)
    {
        return;
    }

    if (!textureCreated_)
    {
        TextureManager::GetInstance()->CreateDynamicTexture(
            kCameraTextureName_,
            frameWidth_,
            frameHeight_,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        );
        textureCreated_ = true;
    }

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
        return false;
    }

    CloseCamera();

    HRESULT result;

    Microsoft::WRL::ComPtr<IMFAttributes> pEnumAttributes;
    result = MFCreateAttributes(&pEnumAttributes, 1);
    if (FAILED(result))
    {
        return false;
    }

    result = pEnumAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(result))
    {
        return false;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    result = MFEnumDeviceSources(pEnumAttributes.Get(), &ppDevices, &deviceCount);
    if (FAILED(result) || deviceIndex >= deviceCount)
    {
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

    result = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&mediaSource_));

    for (UINT32 i = 0; i < deviceCount; i++)
    {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(result))
    {
        return false;
    }

    // SourceReaderの属性を設定（自動変換を有効にする）
    Microsoft::WRL::ComPtr<IMFAttributes> pReaderAttributes;
    result = MFCreateAttributes(&pReaderAttributes, 1);
    if (SUCCEEDED(result))
    {
        // ビデオプロセッサを有効にして自動変換を許可
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    result = MFCreateSourceReaderFromMediaSource(
        mediaSource_.Get(),
        pReaderAttributes.Get(),
        &sourceReader_
    );
    if (FAILED(result))
    {
        CloseCamera();
        return false;
    }

    // RGB32形式を要求
    Microsoft::WRL::ComPtr<IMFMediaType> pType;
    result = MFCreateMediaType(&pType);
    if (FAILED(result))
    {
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
        sprintf_s(buf, "SetCurrentMediaType RGB32 failed: 0x%08X\n", result);
        OutputDebugStringA(buf);
        CloseCamera();
        return false;
    }

    // 実際に設定されたメディアタイプを取得
    Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
    result = sourceReader_->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pCurrentType
    );

    if (SUCCEEDED(result)){

        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        frameWidth_ = width;
        frameHeight_ = height;

        GUID subtype;
        pCurrentType->GetGUID(MF_MT_SUBTYPE, &subtype);

        char buf[512];
        sprintf_s(buf, "Camera: %u x %u\n", width, height);
        OutputDebugStringA(buf);

        if (subtype == MFVideoFormat_RGB32){

            OutputDebugStringA("Format: RGB32 (OK)\n");

        } else if (subtype == MFVideoFormat_NV12){

            OutputDebugStringA("Format: NV12 (変換失敗)\n");

        } else{

            OutputDebugStringA("Format: Other\n");

        }
    }

    isOpened_ = true;
    return true;
}

void CameraCapture::CloseCamera()
{
    if (sourceReader_)
    {
        sourceReader_.Reset();
    }

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
}

bool CameraCapture::CaptureFrame()
{
    if (!isOpened_ || !sourceReader_)
    {
        return false;
    }

    if (frameWidth_ == 0 || frameHeight_ == 0)
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

    // 実際のピクセル数を計算
    uint32_t expectedPixelCount = frameWidth_ * frameHeight_;
    uint32_t actualPixelCount = currentLength / 4;
    uint32_t pixelCount = (actualPixelCount < expectedPixelCount) ? actualPixelCount : expectedPixelCount;

    frameBuffer_.resize(pixelCount * 4);

    // MediaFoundationのRGB32はBGRA順
    // テクスチャはRGBA順（R8G8B8A8_UNORM）
    for (uint32_t i = 0; i < pixelCount; i++)
    {
        frameBuffer_[i * 4 + 0] = pData[i * 4 + 2];  // R ← B
        frameBuffer_[i * 4 + 1] = pData[i * 4 + 1];  // G ← G
        frameBuffer_[i * 4 + 2] = pData[i * 4 + 0];  // B ← R
        frameBuffer_[i * 4 + 3] = 255;               // A
    }

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

        if (!frameBuffer_.empty())
        {
            ImGui::Text("Buffer size: %zu bytes", frameBuffer_.size());
        }
    } else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Camera: CLOSED");
    }
}
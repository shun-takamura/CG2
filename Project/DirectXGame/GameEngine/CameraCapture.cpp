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

void CameraCapture::CaptureThreadFunc()
{
    // このスレッド用にCOMを初期化
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (threadRunning_)
    {
        if (!sourceReader_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> pSample;

        HRESULT result = sourceReader_->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );

        if (SUCCEEDED(result) && pSample)
        {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
            result = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(result))
            {
                BYTE* pData = nullptr;
                DWORD maxLength = 0;
                DWORD currentLength = 0;

                result = pBuffer->Lock(&pData, &maxLength, &currentLength);

                if (SUCCEEDED(result))
                {
                    std::lock_guard<std::mutex> lock(bufferMutex_);

                    frameBufferThread_.resize(currentLength);
                    memcpy(frameBufferThread_.data(), pData, currentLength);

                    // アルファ値を255に
                    for (uint32_t i = 3; i < currentLength; i += 4)
                    {
                        frameBufferThread_[i] = 255;
                    }

                    newFrameAvailable_ = true;

                    pBuffer->Unlock();
                }
            }
        }
    }

    CoUninitialize();
}

void CameraCapture::UpdateTexture()
{
    if (!isOpened_)
    {
        return;
    }

    // 新しいフレームがあればメインバッファにコピー
    if (newFrameAvailable_)
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        frameBufferMain_ = frameBufferThread_;
        newFrameAvailable_ = false;
    }

    if (frameBufferMain_.empty() || frameWidth_ == 0 || frameHeight_ == 0)
    {
        return;
    }

    if (!textureCreated_)
    {
        TextureManager::GetInstance()->CreateDynamicTexture(
            kCameraTextureName_,
            frameWidth_,
            frameHeight_,
            DXGI_FORMAT_B8G8R8A8_UNORM
        );
        textureCreated_ = true;
    }

    if (textureCreated_)
    {
        TextureManager::GetInstance()->UpdateDynamicTexture(
            kCameraTextureName_,
            frameBufferMain_.data(),
            frameBufferMain_.size()
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

    Microsoft::WRL::ComPtr<IMFAttributes> pReaderAttributes;
    result = MFCreateAttributes(&pReaderAttributes, 1);
    if (SUCCEEDED(result))
    {
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
        CloseCamera();
        return false;
    }

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
    }

    isOpened_ = true;

    // キャプチャスレッド開始
    threadRunning_ = true;
    captureThread_ = std::thread(&CameraCapture::CaptureThreadFunc, this);

    return true;
}

void CameraCapture::CloseCamera()
{
    // スレッド停止
    threadRunning_ = false;
    if (captureThread_.joinable())
    {
        captureThread_.join();
    }

    if (sourceReader_)
    {
        sourceReader_.Reset();
    }

    if (mediaSource_)
    {
        mediaSource_->Shutdown();
        mediaSource_.Reset();
    }

    frameBufferMain_.clear();
    frameBufferThread_.clear();
    frameWidth_ = 0;
    frameHeight_ = 0;
    isOpened_ = false;
    textureCreated_ = false;
    newFrameAvailable_ = false;
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

        if (!frameBufferMain_.empty())
        {
            ImGui::Text("Buffer size: %zu bytes", frameBufferMain_.size());
        }
    } else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Camera: CLOSED");
    }
}
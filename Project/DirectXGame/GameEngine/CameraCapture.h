#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include "ConvertString.h"
#include "TextureManager.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

struct CameraDeviceInfo
{
    std::wstring name;
    std::wstring symbolicLink;
    Microsoft::WRL::ComPtr<IMFActivate> activate;
};

class CameraCapture
{
public:
    static CameraCapture* GetInstance();

    void Initialize();
    void Finalize();

    void UpdateTexture();

    static const std::string& GetTextureName() { return kCameraTextureName_; }

    void EnumerateDevices();
    const std::vector<CameraDeviceInfo>& GetDevices() const { return devices_; }

    bool OpenCamera(uint32_t deviceIndex);
    void CloseCamera();
    bool IsOpened() const { return isOpened_; }

    const std::vector<uint8_t>& GetFrameData() const { return frameBufferMain_; }
    uint32_t GetFrameWidth() const { return frameWidth_; }
    uint32_t GetFrameHeight() const { return frameHeight_; }

    void LogDevicesToImGui();

private:
    static inline const std::string kCameraTextureName_ = "##CameraTexture##";
    bool textureCreated_ = false;

    CameraCapture() = default;
    ~CameraCapture() = default;
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    std::vector<CameraDeviceInfo> devices_;

    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource_;
    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader_;
    bool isOpened_ = false;

    // メインスレッド用バッファ
    std::vector<uint8_t> frameBufferMain_;
    uint32_t frameWidth_ = 0;
    uint32_t frameHeight_ = 0;

    // スレッド用
    std::thread captureThread_;
    std::mutex bufferMutex_;
    std::atomic<bool> threadRunning_ = false;
    std::atomic<bool> newFrameAvailable_ = false;
    std::vector<uint8_t> frameBufferThread_;

    void CaptureThreadFunc();
};
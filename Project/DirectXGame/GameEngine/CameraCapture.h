#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <cstdint>
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

    /// <summary>
    /// カメラ映像をテクスチャとして登録・更新
    /// </summary>
    void UpdateTexture();

    /// <summary>
    /// カメラテクスチャの名前を取得（SpriteInstanceで使用）
    /// </summary>
    static const std::string& GetTextureName() { return kCameraTextureName_; }

    // デバイス列挙
    void EnumerateDevices();
    const std::vector<CameraDeviceInfo>& GetDevices() const { return devices_; }

    // カメラ操作
    bool OpenCamera(uint32_t deviceIndex);
    void CloseCamera();
    bool IsOpened() const { return isOpened_; }

    // フレーム取得
    bool CaptureFrame();
    const std::vector<uint8_t>& GetFrameData() const { return frameBuffer_; }
    uint32_t GetFrameWidth() const { return frameWidth_; }
    uint32_t GetFrameHeight() const { return frameHeight_; }

    // ImGuiログ出力
    void LogDevicesToImGui();

private:
    static inline const std::string kCameraTextureName_ = "##CameraTexture##";
    bool textureCreated_ = false;

    CameraCapture() = default;
    ~CameraCapture() = default;
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    std::vector<CameraDeviceInfo> devices_;

    // カメラ関連
    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource_;
    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader_;
    bool isOpened_ = false;

    // フレームデータ
    std::vector<uint8_t> frameBuffer_;
    uint32_t frameWidth_ = 0;
    uint32_t frameHeight_ = 0;
};
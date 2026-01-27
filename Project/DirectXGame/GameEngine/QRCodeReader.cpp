#include "QRCodeReader.h"
#include <imgui.h>

extern "C" {
#include "quirc.h"
}

QRCodeReader* QRCodeReader::GetInstance()
{
    static QRCodeReader instance;
    return &instance;
}

bool QRCodeReader::Decode(const uint8_t* rgbaData, uint32_t width, uint32_t height)
{
    hasDetected_ = false;
    qrCodeData_.clear();

    if (rgbaData == nullptr || width == 0 || height == 0)
    {
        return false;
    }

    // quircオブジェクトを作成
    struct quirc* qr = quirc_new();
    if (!qr)
    {
        return false;
    }

    // 画像サイズを設定
    if (quirc_resize(qr, width, height) < 0)
    {
        quirc_destroy(qr);
        return false;
    }

    // グレースケール画像を取得するバッファ
    uint8_t* image = quirc_begin(qr, nullptr, nullptr);

    // RGBAからグレースケールに変換
    uint32_t pixelCount = width * height;
    for (uint32_t i = 0; i < pixelCount; i++)
    {
        uint8_t r = rgbaData[i * 4 + 0];
        uint8_t g = rgbaData[i * 4 + 1];
        uint8_t b = rgbaData[i * 4 + 2];
        // 標準的なグレースケール変換
        image[i] = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
    }

    // QRコードを検出
    quirc_end(qr);

    // 検出されたQRコードの数を取得
    int count = quirc_count(qr);

    if (count > 0)
    {
        // 最初のQRコードを取得
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(qr, 0, &code);

        // デコード
        quirc_decode_error_t err = quirc_decode(&code, &data);

        if (err == QUIRC_SUCCESS)
        {
            qrCodeData_ = std::string(reinterpret_cast<char*>(data.payload), data.payload_len);
            hasDetected_ = true;
        }
    }

    quirc_destroy(qr);

    return hasDetected_;
}

void QRCodeReader::Reset()
{
    qrCodeData_.clear();
    hasDetected_ = false;
}

void QRCodeReader::OnImGui()
{
    ImGui::Text("=== QR Code Reader ===");

    if (hasDetected_)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "QR Code Detected!");
        ImGui::TextWrapped("Data: %s", qrCodeData_.c_str());
    } else
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No QR code detected");
    }
}
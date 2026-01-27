#pragma once
#include <string>
#include <vector>
#include <cstdint>

class QRCodeReader
{
public:
    static QRCodeReader* GetInstance();

    /// <summary>
    /// RGBAの画像データからQRコードを読み取る
    /// </summary>
    /// <param name="rgbaData">RGBA形式の画像データ</param>
    /// <param name="width">画像の幅</param>
    /// <param name="height">画像の高さ</param>
    /// <returns>QRコードが検出されたらtrue</returns>
    bool Decode(const uint8_t* rgbaData, uint32_t width, uint32_t height);

    /// <summary>
    /// 読み取ったQRコードのデータを取得
    /// </summary>
    const std::string& GetData() const { return qrCodeData_; }

    /// <summary>
    /// QRコードが検出されているか
    /// </summary>
    bool HasDetected() const { return hasDetected_; }

    /// <summary>
    /// 検出状態をリセット
    /// </summary>
    void Reset();

    /// <summary>
    /// ImGuiで情報を表示
    /// </summary>
    void OnImGui();

private:
    QRCodeReader() = default;
    ~QRCodeReader() = default;
    QRCodeReader(const QRCodeReader&) = delete;
    QRCodeReader& operator=(const QRCodeReader&) = delete;

    std::string qrCodeData_;
    bool hasDetected_ = false;
};
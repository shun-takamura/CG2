#pragma once
#include <string>
#include <vector>
#include <cstdint>

/// <summary>
/// カメラの映像から QR コードを読み取る。
/// シングルトンにする理由：カメラの映像を読む読み取り器を 1 つだけ持ち、結果をどこからでも見られるようにするため。
/// </summary>
class QRCodeReader
{
public:
    /// <summary>唯一のインスタンス</summary>
    /// <returns>インスタンス</returns>
    static QRCodeReader* GetInstance();

    /// <summary>
    /// 4 バイト／画素の画像データをグレースケールにして QR コードを読み取る（最初の 1 つだけ）。前回の結果は消す
    /// </summary>
    /// <param name="rgbaData">RGBA形式の画像データ</param>
    /// <param name="width">画像の幅</param>
    /// <param name="height">画像の高さ</param>
    /// <returns>QR コードを読み取れたら true（データが nullptr・大きさ 0・検出できない時は false）</returns>
    bool Decode(const uint8_t* rgbaData, uint32_t width, uint32_t height);

    /// <summary>
    /// 読み取った QR コードのデータを取得する
    /// </summary>
    /// <returns>データ（末尾の空白・改行・ヌルは除く。未検出なら空）</returns>
    const std::string& GetData() const { return qrCodeData_; }

    /// <summary>
    /// QRコードが検出されているか
    /// </summary>
    /// <returns>検出されていれば true</returns>
    bool HasDetected() const { return hasDetected_; }

    /// <summary>
    /// 検出状態とデータを消す
    /// </summary>
    void Reset();

    /// <summary>
    /// 検出状態とデータを ImGui に表示する
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
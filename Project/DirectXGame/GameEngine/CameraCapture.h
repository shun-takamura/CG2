#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>   // std::thread 別スレッドを作るためのライブラリ
#include <mutex>    // std::mutex スレッド間の排他制御用
#include <atomic>   // std::atomic スレッド間で安全に変数を共有するため
#include "ConvertString.h"
#include "TextureManager.h"

// MediaFoundation用のライブラリをリンク
#pragma comment(lib, "mfplat.lib")    // Media Foundation Platform
#pragma comment(lib, "mf.lib")        // Media Foundation Core
#pragma comment(lib, "mfreadwrite.lib") // SourceReader用
#pragma comment(lib, "mfuuid.lib")    // GUIDの定義

/// <summary>
/// カメラデバイスの情報を格納する構造体
/// </summary>
struct CameraDeviceInfo
{
    std::wstring name;           // デバイス名（例: "HD Webcam"）
    std::wstring symbolicLink;   // デバイスの識別子（システムが使う内部ID）
    Microsoft::WRL::ComPtr<IMFActivate> activate;  // デバイスを起動するためのオブジェクト
};

/// <summary>
/// カメラからの映像取得を管理するクラス
/// シングルトンパターンで実装
/// 
/// 【重要】非同期（マルチスレッド）で動作
/// - メインスレッド: ゲームのロジックと描画を担当
/// - キャプチャスレッド: カメラからの映像取得を担当
/// 
/// これにより、カメラ取得の待ち時間がゲームのFPSに影響しない
/// シングルトンにする理由：Web カメラのデバイスは 1 台を 1 か所で開いて共有するため。
/// </summary>
class CameraCapture
{
public:
    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    /// <returns>インスタンス</returns>
    static CameraCapture* GetInstance();

    /// <summary>接続されているカメラを列挙する（起動時に 1 回）</summary>
    void Initialize();
    /// <summary>カメラを閉じてキャプチャスレッドを止め、列挙したデバイスを捨てる</summary>
    void Finalize();

    /// <summary>
    /// カメラ映像をDirectXテクスチャに転送する
    /// 毎フレーム Draw() の最初で呼ぶ。初めてのフレームで動的テクスチャを作る（カメラが開いていなければ何もしない）
    /// </summary>
    void UpdateTexture();

    /// <summary>
    /// カメラテクスチャの識別名を取得
    /// SpriteInstance でテクスチャを指定するときに使う
    /// </summary>
    /// <returns>テクスチャ名</returns>
    static const std::string& GetTextureName() { return kCameraTextureName_; }

    /// <summary>
    /// PCに接続されているカメラデバイスを列挙する
    /// </summary>
    void EnumerateDevices();
    /// <summary>列挙したカメラデバイスの一覧</summary>
    /// <returns>一覧</returns>
    const std::vector<CameraDeviceInfo>& GetDevices() const { return devices_; }

    /// <summary>
    /// 指定したインデックスのカメラを開く
    /// 開いていたカメラは先に閉じる。これを呼ぶとキャプチャスレッドが開始される
    /// </summary>
    /// <param name="deviceIndex">EnumerateDevices で並んだ番号</param>
    /// <returns>開けたら true（番号が範囲外・デバイスや読み取り器を作れなかったら false）</returns>
    bool OpenCamera(uint32_t deviceIndex);

    /// <summary>
    /// カメラを閉じる
    /// キャプチャスレッドも停止する
    /// </summary>
    void CloseCamera();

    /// <summary>カメラが開いているか</summary>
    /// <returns>開いていれば true</returns>
    bool IsOpened() const { return isOpened_; }

    /// <summary>
    /// 現在のフレームデータを取得（QRコード読み取りなどで使用）
    /// </summary>
    /// <returns>BGRA 32bit の画素（カメラが開いていない・まだ届いていなければ空）</returns>
    const std::vector<uint8_t>& GetFrameData() const { return frameBufferMain_; }
    /// <summary>フレームの幅</summary>
    /// <returns>幅 [px]</returns>
    uint32_t GetFrameWidth() const { return frameWidth_; }
    /// <summary>フレームの高さ</summary>
    /// <returns>高さ [px]</returns>
    uint32_t GetFrameHeight() const { return frameHeight_; }

    /// <summary>カメラデバイスの一覧を ImGui に表示する（USE_IMGUI の時だけ）</summary>
    void LogDevicesToImGui();

private:
    // テクスチャの識別名（他のテクスチャと被らないように##で囲む）
    static inline const std::string kCameraTextureName_ = "##CameraTexture##";
    bool textureCreated_ = false;

    // シングルトンパターン: コンストラクタをprivateにして外部からnewできないようにする
    CameraCapture() = default;
    ~CameraCapture() = default;
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    // 列挙されたカメラデバイスのリスト
    std::vector<CameraDeviceInfo> devices_;

    // ===== Media Foundation関連 =====
    // MediaSource: カメラデバイス自体を表すオブジェクト
    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource_;
    // SourceReader: MediaSourceからフレームを読み取るオブジェクト
    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader_;
    bool isOpened_ = false;

    // ===== メインスレッド用 =====
    // メインスレッドで使うバッファ（描画やQR読み取りに使用）
    std::vector<uint8_t> frameBufferMain_;
    uint32_t frameWidth_ = 0;
    uint32_t frameHeight_ = 0;

    // ===== マルチスレッド関連 =====
    // captureThread_: カメラ取得を行う別スレッド
    std::thread captureThread_;

    // bufferMutex_: 排他制御用のロック
    // 2つのスレッドが同時にframeBufferThread_にアクセスしないようにする
    std::mutex bufferMutex_;

    // threadRunning_: スレッドを動かし続けるかどうかのフラグ
    // atomic型なので、スレッド間で安全に読み書きできる
    std::atomic<bool> threadRunning_ = false;

    // newFrameAvailable_: 新しいフレームが準備できたかどうか
    std::atomic<bool> newFrameAvailable_ = false;

    // frameBufferThread_: キャプチャスレッドが書き込むバッファ
    std::vector<uint8_t> frameBufferThread_;

    /// <summary>
    /// キャプチャスレッドで実行される関数
    /// カメラからフレームを取得し続け、届いたらスレッド側のバッファへ写す（threadRunning_ が false になるまで）
    /// </summary>
    void CaptureThreadFunc();
};
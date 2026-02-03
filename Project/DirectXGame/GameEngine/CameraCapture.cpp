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
    // 起動時にカメラデバイスを列挙
    EnumerateDevices();
}

void CameraCapture::Finalize()
{
    // 終了時にカメラを閉じてリソースを解放
    CloseCamera();
    devices_.clear();
}

void CameraCapture::CaptureThreadFunc()
{
    // ===== COMの初期化（スレッドごとに必要）=====
    // Media FoundationはCOMを使うので、スレッドごとに初期化が必要
    // COINIT_MULTITHREADED: マルチスレッドモードで初期化
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        // COM初期化失敗時はスレッドを終了
        return;
    }

    // メインのキャプチャループ
    // threadRunning_がtrueの間、ずっとカメラからフレームを取得し続ける
    while (threadRunning_)
    {
        // SourceReaderがなければ待機（カメラが開かれるまで）
        if (!sourceReader_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ===== カメラからフレームを読み取り =====
        DWORD streamIndex = 0;    // 使用するストリーム（通常は0）
        DWORD flags = 0;          // 読み取り結果のフラグ
        LONGLONG timestamp = 0;   // フレームのタイムスタンプ
        Microsoft::WRL::ComPtr<IMFSample> pSample;  // フレームデータが入る

        // ReadSample(): カメラから1フレーム取得
        // この関数は同期的に動作し、フレームが来るまでブロックする
        // だから別スレッドで実行している！（メインスレッドをブロックしないため）
        HRESULT result = sourceReader_->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,  // 最初のビデオストリームを使用
            0,              // フラグ（通常は0）
            &streamIndex,   // [出力] ストリームインデックス
            &flags,         // [出力] 状態フラグ
            &timestamp,     // [出力] タイムスタンプ
            &pSample        // [出力] フレームデータ
        );

        // フレーム取得成功したら処理
        if (SUCCEEDED(result) && pSample)
        {
            // ===== フレームデータをバッファに変換 =====
            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
            // ConvertToContiguousBuffer(): 複数のバッファを1つにまとめる
            result = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(result))
            {
                BYTE* pData = nullptr;        // ピクセルデータへのポインタ
                DWORD maxLength = 0;          // バッファの最大サイズ
                DWORD currentLength = 0;      // 実際のデータサイズ

                // Lock(): バッファをロックしてポインタを取得
                // ロック中は他からアクセスできない
                result = pBuffer->Lock(&pData, &maxLength, &currentLength);

                if (SUCCEEDED(result))
                {
                    // ===== 排他制御してバッファにコピー =====
                    // lock_guard: このスコープ内でmutexをロック
                    // スコープを抜けると自動的にアンロックされる
                    // これにより、メインスレッドと同時にアクセスすることを防ぐ
                    std::lock_guard<std::mutex> lock(bufferMutex_);

                    // データをスレッド用バッファにコピー
                    frameBufferThread_.resize(currentLength);
                    memcpy(frameBufferThread_.data(), pData, currentLength);

                    // カメラのBGRA形式ではアルファが0の場合があるため
                    // 4バイトごと（BGRA）の4番目（A）を255に
                    for (uint32_t i = 3; i < currentLength; i += 4)
                    {
                        // アルファ値を255に設定
                        frameBufferThread_[i] = 255;
                    }

                    // 新しいフレームが準備できたことをフラグで通知
                    newFrameAvailable_ = true;

                    // Unlock(): バッファのロックを解除
                    pBuffer->Unlock();
                }
            }
        }
    }

    // スレッド終了時にCOMを解放
    CoUninitialize();
}

void CameraCapture::UpdateTexture()
{
    // カメラが開いていなければ何もしない
    if (!isOpened_)
    {
        return;
    }

    // ===== キャプチャスレッドからデータを受け取る =====
    // 新しいフレームがあれば、スレッド用バッファからメイン用バッファにコピー
    if (newFrameAvailable_)
    {
        // lock_guard: バッファアクセス中はロック
        std::lock_guard<std::mutex> lock(bufferMutex_);

        // スレッド用バッファ → メイン用バッファにコピー
        frameBufferMain_ = frameBufferThread_;

        // フラグをリセット
        newFrameAvailable_ = false;
    }

    // バッファが空なら何もしない
    if (frameBufferMain_.empty() || frameWidth_ == 0 || frameHeight_ == 0)
    {
        return;
    }

    // テクスチャを作成(初回のみ)
    if (!textureCreated_)
    {
        TextureManager::GetInstance()->CreateDynamicTexture(
            kCameraTextureName_,
            frameWidth_,
            frameHeight_,
            DXGI_FORMAT_B8G8R8A8_UNORM  // BGRAフォーマット
        );
        textureCreated_ = true;
    }

    // テクスチャにデータを転送
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
    // 既存のリストをクリア
    devices_.clear();

    HRESULT result;

    // 列挙用の属性オブジェクトを作成
    Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
    result = MFCreateAttributes(&pAttributes, 1);
    assert(SUCCEEDED(result));

    // 「ビデオキャプチャデバイスを探す」という属性を設定
    result = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,           // 属性の種類
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID  // ビデオキャプチャ
    );
    assert(SUCCEEDED(result));

    // デバイスを列挙
    IMFActivate** ppDevices = nullptr;  // デバイスの配列（出力）
    UINT32 deviceCount = 0;             // 見つかったデバイス数（出力）

    // MFEnumDeviceSources(): 条件に合うデバイスを列挙
    result = MFEnumDeviceSources(pAttributes.Get(), &ppDevices, &deviceCount);

    if (FAILED(result) || deviceCount == 0)
    {
        return;  // デバイスが見つからなかった
    }

    // 各デバイスの情報を取得
    for (UINT32 i = 0; i < deviceCount; i++)
    {
        CameraDeviceInfo info;
        info.activate = ppDevices[i];

        // デバイス名を取得
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
            CoTaskMemFree(friendlyName);  // メモリ解放
        }

        // シンボリックリンク（デバイスの内部識別子）を取得
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

        // リストに追加
        devices_.push_back(std::move(info));
    }

    // 配列のメモリを解放
    CoTaskMemFree(ppDevices);
}

bool CameraCapture::OpenCamera(uint32_t deviceIndex)
{
    // インデックスが範囲外ならエラー
    if (deviceIndex >= devices_.size())
    {
        return false;
    }

    // 既に開いていたら一度閉じる
    CloseCamera();

    HRESULT result;

    // デバイスを再列挙
    // EnumerateDevicesで取得したactivateは古くなっている可能性があるので新しく列挙し直す
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

    // カメラデバイスを起動
    // ActivateObject(): デバイスを起動してMediaSourceを取得
    result = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&mediaSource_));

    // 列挙したデバイスを解放
    for (UINT32 i = 0; i < deviceCount; i++)
    {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(result))
    {
        return false;
    }

    // SourceReaderの属性を設定
    Microsoft::WRL::ComPtr<IMFAttributes> pReaderAttributes;
    result = MFCreateAttributes(&pReaderAttributes, 1);
    if (SUCCEEDED(result))
    {
        // VIDEO_PROCESSING: フォーマット変換を自動で行う
        // これがないとNV12などの形式をRGB32に変換できない
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    // SourceReaderを作成
    // SourceReader: MediaSourceからフレームを読み取るオブジェクト
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

    // 出力フォーマットを設定
    Microsoft::WRL::ComPtr<IMFMediaType> pType;
    result = MFCreateMediaType(&pType);
    if (FAILED(result))
    {
        CloseCamera();
        return false;
    }

    // RGB32形式（BGRA、各8bit）を要求
    pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);     // ビデオ
    pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);     // RGB32形式

    // フォーマットを設定
    result = sourceReader_->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,  // 最初のビデオストリーム
        nullptr,
        pType.Get()
    );
    if (FAILED(result))
    {
        CloseCamera();
        return false;
    }

    // 実際に設定されたフォーマットを取得
    Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
    result = sourceReader_->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pCurrentType
    );

    if (SUCCEEDED(result))
    {
        // 解像度を取得
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        frameWidth_ = width;
        frameHeight_ = height;
    }

    isOpened_ = true;

    // キャプチャスレッドを開始
    // std::thread(): 新しいスレッドを作成
    // 第1引数: 実行する関数（CaptureThreadFunc）
    // 第2引数: thisポインタ（メンバ関数なので必要）
    threadRunning_ = true;
    captureThread_ = std::thread(&CameraCapture::CaptureThreadFunc, this);

    return true;
}

void CameraCapture::CloseCamera()
{
    // キャプチャスレッドを停止
    // まずフラグをfalseにしてループを終了させる
    threadRunning_ = false;

    // joinable(): スレッドが実行中かどうか
    // join(): スレッドが終了するまで待機
    if (captureThread_.joinable())
    {
        captureThread_.join();  // スレッドの終了を待つ
    }

    // MediaFoundationリソースを解放
    if (sourceReader_)
    {
        sourceReader_.Reset();
    }

    if (mediaSource_)
    {
        mediaSource_->Shutdown();  // MediaSourceは明示的にShutdownが必要
        mediaSource_.Reset();
    }

    // バッファとフラグをクリア
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
#ifdef _DEBUG

    //ImGui::Text("=== Camera Devices ===");

    //if (devices_.empty())
    //{
    //    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No camera devices found");
    //} else
    //{
    //    ImGui::Text("Found %zu device(s):", devices_.size());
    //    ImGui::Separator();

    //    // 各デバイスにOpenボタンを表示
    //    for (size_t i = 0; i < devices_.size(); i++)
    //    {
    //        const auto& device = devices_[i];
    //        std::string nameUtf8 = ConvertString(device.name);

    //        // ##でIDを付けてボタンを区別
    //        std::string buttonLabel = "Open##" + std::to_string(i);
    //        if (ImGui::Button(buttonLabel.c_str()))
    //        {
    //            OpenCamera(static_cast<uint32_t>(i));
    //        }
    //        ImGui::SameLine();
    //        ImGui::Text("[%zu] %s", i, nameUtf8.c_str());
    //    }
    //}

    //ImGui::Separator();

    //if (ImGui::Button("Refresh Devices"))
    //{
    //    EnumerateDevices();
    //}

    //ImGui::Separator();
    //ImGui::Text("=== Camera Status ===");

    //if (isOpened_)
    //{
    //    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Camera: OPENED");
    //    ImGui::Text("Resolution: %u x %u", frameWidth_, frameHeight_);

    //    if (ImGui::Button("Close Camera"))
    //    {
    //        CloseCamera();
    //    }

    //    if (!frameBufferMain_.empty())
    //    {
    //        ImGui::Text("Buffer size: %zu bytes", frameBufferMain_.size());
    //    }
    //} else
    //{
    //    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Camera: CLOSED");
    //}

#endif // DEBUG
}
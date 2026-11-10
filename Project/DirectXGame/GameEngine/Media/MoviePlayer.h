#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

/// <summary>
/// 動画ファイル（H.264 の mp4 を想定）をループで流し、TextureManager の動的テクスチャ（BGRA）へ送るクラス。
/// CameraCapture と同じ作り：Media Foundation の SourceReader を別スレッドで回し、最新の 1 フレームだけを受け渡す。
/// - 再生の速さは実時間（steady_clock）。ゲームのスロー・一時停止の影響を受けない
/// - 映像のストリームだけ読む（音は出さない）
/// - ファイルを開く処理もスレッドの中で行うので、Start でメインスレッドは止まらない
/// - ファイルは FS から直接読む（Assets.pack には入れない。配布 ZIP へは package_release.py の FS_INCLUDE_DIRS で入れる）
/// 使い方：Start(path) → 毎フレーム Draw の中で UpdateTexture() → HasFrame() ならテクスチャ名でスプライトを描く → Stop()
/// </summary>
class MoviePlayer {
public:
	MoviePlayer() = default;
	~MoviePlayer();
	MoviePlayer(const MoviePlayer&) = delete;
	MoviePlayer& operator=(const MoviePlayer&) = delete;

	/// <summary>
	/// 先頭から再生を始める（再生中なら一度止めてから）。ファイルが無ければ false。
	/// textureBaseName はテクスチャ名の元（実際の名前は "_幅x高さ" が付く。GetTextureName で取る）
	/// </summary>
	bool Start(const std::string& filePath, const std::string& textureBaseName);

	/// <summary>再生を止めてスレッドを片付ける（テクスチャは TextureManager に残り、次の Start で使い回す）</summary>
	void Stop();

	/// <summary>
	/// 新しいフレームがあればテクスチャへ送る（コマンドリストにコピーを積むので、Draw の中で呼ぶ）。
	/// 初めてのフレームでテクスチャを作る
	/// </summary>
	void UpdateTexture();

	bool IsPlaying() const { return threadRunning_; }
	/// <summary>開けなかった・読めなくなった（Start し直すまで true）</summary>
	bool HasFailed() const { return failed_; }
	/// <summary>テクスチャに 1 フレーム以上送ってある（描いてよい）</summary>
	bool HasFrame() const { return hasFrame_; }

	const std::string& GetTextureName() const { return textureName_; }
	uint32_t GetWidth() const { return textureWidth_; }
	uint32_t GetHeight() const { return textureHeight_; }

private:
	void ThreadFunc(std::wstring path);
	/// <summary>読み取り器を作って出力を BGRA にする。成功したら frameWidth_ などを入れる</summary>
	bool OpenReader(const std::wstring& path);
	void ReadCurrentMediaType();
	/// <summary>サンプルの画素を詰めて（行の幅のずれを吸収して）スレッド側のバッファへ写す</summary>
	void CopySample(IMFSample* sample);

	Microsoft::WRL::ComPtr<IMFSourceReader> reader_;  // スレッドの中だけで触る

	std::thread thread_;
	std::atomic<bool> threadRunning_ = false;
	std::atomic<bool> failed_ = false;

	// ----- スレッド → メインの受け渡し（bufferMutex_ で守る） -----
	std::mutex bufferMutex_;
	std::vector<uint8_t> threadBuffer_;
	uint32_t threadWidth_ = 0;
	uint32_t threadHeight_ = 0;
	bool newFrame_ = false;

	// ----- スレッドの中だけで使う -----
	uint32_t frameWidth_ = 0;
	uint32_t frameHeight_ = 0;
	LONG defaultStride_ = 0;
	std::vector<uint8_t> workBuffer_;

	// ----- メインスレッド -----
	std::vector<uint8_t> mainBuffer_;
	std::string textureBaseName_;
	std::string textureName_;
	uint32_t textureWidth_ = 0;
	uint32_t textureHeight_ = 0;
	bool hasFrame_ = false;
};

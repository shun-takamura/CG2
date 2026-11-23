#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class DirectXCore;
struct AnimatedMeshData;

/// <summary>
/// 次のシーンのアセット（シェーダ・テクスチャ・静的モデル・アニメーションするモデルと .anim）を、今のシーンを動かしたまま裏で読んでおく。
/// 一度読んだアセットはシーンをまたいで残るので、次のシーンの Initialize はキャッシュに当たるだけになる。
///
/// 何を読むかは手で書かず、シーンを読んだときに実際に読まれた物を記録して
/// Resources/Json/Preload/<シーン名>.json に書き出したもの（マニフェスト）を使う（記録は Debug だけ）。
///
/// 段階：
///   1. 別スレッド：シェーダ（DirectXCore のキャッシュへ）・テクスチャの CPU フェーズ・
///                 アニメーションするモデルの CPU フェーズと .anim（AnimatedMeshCache）
///   2. メイン    ：テクスチャとアニメーションするモデルの GPU フェーズを 1 フレーム数 ms ずつ。DirectStorage へは
///                 待たずに送り、fence が終わった物からバリアを積んで仕上げる
///   3. 別スレッド：モデルの CPU フェーズ（テクスチャが全部そろってから。モデルの GPU フェーズがテクスチャを引くため）
///   4. メイン    ：モデルの GPU フェーズを 1 フレーム数 ms ずつ。テクスチャと同じく DirectStorage へは待たずに送り、
///                 fence が終わった物から仕上げる
///
/// SRV ヒープ・コマンドリスト・DirectStorage への登録はメインスレッドでしか触らない。
/// シングルトンにする理由：裏で読み込むスレッドと、読み込み済みの表をプロセスに 1 つだけ持つため。
/// </summary>
class ScenePreloader {
public:
	/// <summary>唯一のインスタンス</summary>
	/// <returns>インスタンス</returns>
	static ScenePreloader* GetInstance();

	/// <summary>シェーダの読み込みとリソース作成に使う DirectXCore を受け取る</summary>
	/// <param name="dxCore">DirectXCore</param>
	void Initialize(DirectXCore* dxCore);
	/// <summary>読み込み中のスレッドを待って止め、先読みの状態を捨てる</summary>
	void Finalize();

	//====================
	// 先読み
	//====================
	/// <summary>
	/// sceneName のマニフェストを読んで先読みを始める（メインスレッド）。マニフェストが無ければ何もしない。
	/// 前の先読みが残っていれば先に終わらせる
	/// </summary>
	/// <param name="sceneName">次のシーンの名前</param>
	void Begin(const std::string& sceneName);
	/// <summary>毎フレーム呼ぶ（メインスレッド）。GPU フェーズを少しずつ進める</summary>
	void Update();
	/// <summary>
	/// 残りを今すぐ全部終わらせる（メインスレッド）。シーンを読む前に必ず呼ぶ：裏のスレッドが CPU フェーズの途中の
	/// テクスチャ・モデルを、シーンの Initialize が同期ロードで触らないように
	/// </summary>
	void CompleteNow();
	/// <summary>先読みの途中か</summary>
	/// <returns>途中なら true</returns>
	bool IsBusy() const { return stage_ != Stage::Idle && stage_ != Stage::Done; }
	/// <summary>読み終えた割合（デバッグ表示用。段階ごとの目安）</summary>
	/// <returns>0〜1</returns>
	float GetProgress() const;

	//====================
	// 記録（シーンの Initialize の間に読まれた物を集める）
	//====================
	/// <summary>
	/// 記録を始める（DirectXCore が要る）。テクスチャ・モデル・アニメーションするモデル・.anim・シェーダの読み込みを見張り、
	/// 読まれた物を重ならないように集める
	/// </summary>
	void BeginRecording();
	/// <summary>集めた物を sceneName のマニフェストとして書き出す（Debug だけ。それ以外の構成は集めた物を捨てる）</summary>
	/// <param name="sceneName">シーンの名前</param>
	void EndRecording(const std::string& sceneName);

private:
	ScenePreloader() = default;
	~ScenePreloader() = default;
	ScenePreloader(const ScenePreloader&) = delete;
	ScenePreloader& operator=(const ScenePreloader&) = delete;

	struct ShaderEntry { std::wstring path; std::wstring profile; };
	struct TextureEntry { std::string path; bool linear = false; };
	struct ModelEntry { std::string directory; std::string file; };
	struct Manifest {
		std::vector<ShaderEntry> shaders;
		std::vector<TextureEntry> textures;
		std::vector<ModelEntry> models;
		std::vector<ModelEntry> animatedModels;
		std::vector<std::string> animations;
		/// <summary>中身を空にする</summary>
		void Clear() { shaders.clear(); textures.clear(); models.clear(); animatedModels.clear(); animations.clear(); }
		/// <summary>何も入っていないか</summary>
		/// <returns>空なら true</returns>
		bool Empty() const { return shaders.empty() && textures.empty() && models.empty() && animatedModels.empty() && animations.empty(); }
		/// <summary>入っている物の合計</summary>
		/// <returns>合計の件数</returns>
		size_t Count() const { return shaders.size() + textures.size() + models.size() + animatedModels.size() + animations.size(); }
	};

	enum class Stage { Idle, CpuTextures, GpuTextures, WaitTextures, CpuModels, GpuModels, WaitModels, Done };

	/// <summary>シーン名からマニフェストのパスを作る</summary>
	/// <param name="sceneName">シーンの名前</param>
	/// <returns>Resources/Json/Preload/シーン名.json</returns>
	static std::string ManifestPath(const std::string& sceneName);
	/// <summary>マニフェストの JSON を読み、out に足す（out は空にしない）</summary>
	/// <param name="path">マニフェストのパス</param>
	/// <param name="out">読んだ物を足す先</param>
	/// <returns>読めたら true（ファイルが無い・JSON が壊れている・ルートがオブジェクトでなければ false）</returns>
	static bool LoadManifest(const std::string& path, Manifest& out);
	/// <summary>マニフェストを JSON に書き出す（フォルダが無ければ作る）</summary>
	/// <param name="path">書き出すパス</param>
	/// <param name="sceneName">シーンの名前（JSON に残す）</param>
	/// <param name="manifest">書き出す物</param>
	/// <returns>書けたら true</returns>
	static bool SaveManifest(const std::string& path, const std::string& sceneName, const Manifest& manifest);

	/// <summary>
	/// 指定の段階の別スレッドを始める（前のスレッドは待ってから）。
	/// CpuTextures はシェーダ・テクスチャ・アニメーションするモデルの CPU フェーズ、それ以外はモデルの CPU フェーズ
	/// </summary>
	/// <param name="stage">始める段階</param>
	void StartWorker(Stage stage);
	/// <summary>別スレッドが動いていれば終わるまで待つ</summary>
	void JoinWorker();
	/// <summary>
	/// テクスチャとアニメーションするモデルの GPU フェーズを予算の時間まで進める（最低 1 件）。
	/// DirectStorage へは待たずに送り、仕上げはバッチとして積む
	/// </summary>
	/// <param name="budgetMs">使ってよい時間 [ms]</param>
	void UpdateGpuTextures(double budgetMs);
	/// <summary>モデルの GPU フェーズを予算の時間まで進める（最低 1 件）。仕上げはバッチとして積む</summary>
	/// <param name="budgetMs">使ってよい時間 [ms]</param>
	void UpdateGpuModels(double budgetMs);
	/// <summary>fence が終わったバッチの仕上げを送った順に呼ぶ（終わっていないバッチで止める）</summary>
	/// <param name="wait">true なら DirectStorage の完了を待ってから仕上げる</param>
	void FinishCompletedTextureBatches(bool wait);
	/// <summary>モデルの CPU フェーズの別スレッドを始める（モデルが無ければそのまま終える）</summary>
	void StartModels();
	/// <summary>先読みを終えた状態にし、件数と時間をログに残す</summary>
	void Finish();

	DirectXCore* dxCore_ = nullptr;

	// 先読み
	std::string sceneName_;
	Manifest manifest_;
	Stage stage_ = Stage::Idle;
	std::thread worker_;
	std::atomic<bool> workerDone_ = false;
	size_t nextTexture_ = 0;
	std::vector<std::shared_ptr<AnimatedMeshData>> pendingMeshes_;  // CPU フェーズを済ませたアニメーションするモデル（別スレッドが詰める）
	size_t nextMesh_ = 0;
	size_t nextModel_ = 0;
	// DirectStorage へ送って仕上げ待ちの物（テクスチャ・モデル。fence が終わったら finishers を呼ぶ）
	struct TextureBatch { uint64_t fence = 0; std::vector<std::function<void()>> finishers; };
	std::vector<TextureBatch> textureBatches_;
	double beginMs_ = 0.0;

	// 記録
	bool recording_ = false;
	Manifest recorded_;
	std::mutex recordMutex_;
};

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
/// </summary>
class ScenePreloader {
public:
	static ScenePreloader* GetInstance();

	void Initialize(DirectXCore* dxCore);
	/// <summary>読み込み中のスレッドを待って止める</summary>
	void Finalize();

	//====================
	// 先読み
	//====================
	/// <summary>
	/// sceneName のマニフェストを読んで先読みを始める（メインスレッド）。マニフェストが無ければ何もしない。
	/// 前の先読みが残っていれば先に終わらせる
	/// </summary>
	void Begin(const std::string& sceneName);
	/// <summary>毎フレーム呼ぶ（メインスレッド）。GPU フェーズを少しずつ進める</summary>
	void Update();
	/// <summary>
	/// 残りを今すぐ全部終わらせる（メインスレッド）。シーンを読む前に必ず呼ぶ：裏のスレッドが CPU フェーズの途中の
	/// テクスチャ・モデルを、シーンの Initialize が同期ロードで触らないように
	/// </summary>
	void CompleteNow();
	/// <summary>先読みの途中か</summary>
	bool IsBusy() const { return stage_ != Stage::Idle && stage_ != Stage::Done; }
	/// <summary>読み終えた割合 0〜1（デバッグ表示用）</summary>
	float GetProgress() const;

	//====================
	// 記録（シーンの Initialize の間に読まれた物を集める）
	//====================
	void BeginRecording();
	/// <summary>集めた物を sceneName のマニフェストとして書き出す（Debug だけ。それ以外の構成は集めた物を捨てる）</summary>
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
		void Clear() { shaders.clear(); textures.clear(); models.clear(); animatedModels.clear(); animations.clear(); }
		bool Empty() const { return shaders.empty() && textures.empty() && models.empty() && animatedModels.empty() && animations.empty(); }
		size_t Count() const { return shaders.size() + textures.size() + models.size() + animatedModels.size() + animations.size(); }
	};

	enum class Stage { Idle, CpuTextures, GpuTextures, WaitTextures, CpuModels, GpuModels, WaitModels, Done };

	static std::string ManifestPath(const std::string& sceneName);
	static bool LoadManifest(const std::string& path, Manifest& out);
	static bool SaveManifest(const std::string& path, const std::string& sceneName, const Manifest& manifest);

	void StartWorker(Stage stage);
	void JoinWorker();
	void UpdateGpuTextures(double budgetMs);
	void UpdateGpuModels(double budgetMs);
	void FinishCompletedTextureBatches(bool wait);
	void StartModels();
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

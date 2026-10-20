#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "ModelInstance.h"
#include "Animation.h"

class DirectXCore;

/// <summary>
/// アニメーションするモデル（.mesh v3/v4 ＋ .skel ＋ .anim）の、インスタンスをまたいで同じになる部分。
/// 頂点・インデックス・スキンの影響度は GPU のバッファごと共有する（スキニングの出力・パレット・マテリアルはインスタンスごと）
/// </summary>
struct AnimatedMeshData {
	std::string key;            // directory/filename（.mesh のパス）

	// CPU 経路（FS）では頂点・インデックス・skinClusterData も持つ。DirectStorage 経路では rootNode と materialData だけ
	ModelData modelData;
	Animation animation;        // 同じ階層の既定の .anim（無ければ空）
	std::string animationPath;
	std::vector<RenderSubmesh> submeshes;  // materialResource / material は空（インスタンスが作る）
	std::string matFilePath;

	bool useDirectStorage = false;
	std::string meshFilePath;
	uint32_t vertexFileOffset = 0;
	uint32_t vertexCount = 0;
	uint32_t indexFileOffset = 0;
	uint32_t indexCount = 0;
	uint32_t skinFileOffset = 0;
	bool hasSkinning = false;
	BoundingSphere bounds;
	std::vector<Matrix4x4> jointInverseBindMatrices;  // .skel の joint-index 順
	std::vector<std::string> jointNames;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;  // DirectStorage 経路だけ（CPU 経路は骨の並びに合わせてインスタンスが詰める）
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};
};

/// <summary>
/// AnimatedMeshData と .anim のクリップのキャッシュ（シングルトン）。一度読んだ物はシーンをまたいで残す。
/// 同じメッシュを何体出しても（HUB の機体・カタパルトの機体・ゲームの自機＝PlayerModel、分身など）読むのは 1 回だけ。
/// ScenePreloader は LoadCPU（別スレッド）→ CreateGPU(wait=false)（メイン）→ fence の後に FinishGPU → Insert で裏から入れる
/// </summary>
class AnimatedMeshCache {
public:
	static AnimatedMeshCache* GetInstance();
	/// <summary>GPU のバッファを手放す（DirectXCore より先に）</summary>
	void Finalize();

	/// <summary>無ければその場で読む（メインスレッド）。読めなければ nullptr</summary>
	std::shared_ptr<const AnimatedMeshData> Acquire(const std::string& directoryPath, const std::string& filename, DirectXCore* dxCore);
	bool Contains(const std::string& directoryPath, const std::string& filename) const;

	/// <summary>.anim のクリップ（無ければ読む。どのスレッドからでもよい）。ファイルが無ければ nullptr</summary>
	std::shared_ptr<const Animation> GetAnimation(const std::string& animPath);

	//====================
	// 裏での先読み用
	//====================
	/// <summary>CPU フェーズ：.mesh のヘッダ・.skel・.anim・.mat のパスを読む（GPU は触らない。どのスレッドからでもよい）</summary>
	static std::shared_ptr<AnimatedMeshData> LoadCPU(const std::string& directoryPath, const std::string& filename);
	/// <summary>
	/// GPU フェーズ：バッファを作って中身を入れる（メインスレッド）。DirectStorage 経路で wait=false なら転送を登録するだけで true を返す：
	/// 呼び出し側が DStorageManager::SubmitWithFence で送り、終わってから FinishGPU を呼ぶ
	/// </summary>
	static bool CreateGPU(AnimatedMeshData& data, DirectXCore* dxCore, bool wait);
	/// <summary>転送が終わった後の仕上げ（インデックスバッファのバリア）。メインスレッド</summary>
	static void FinishGPU(AnimatedMeshData& data, DirectXCore* dxCore);
	/// <summary>仕上がった物を入れる。同じキーが既にあれば何もしない</summary>
	void Insert(std::shared_ptr<AnimatedMeshData> data);

	/// <summary>Acquire / GetAnimation が呼ばれるたびに知らせる（シーンの読み込みの記録用）。空の関数で外す</summary>
	void SetLoadObserver(std::function<void(const std::string& directoryPath, const std::string& filename)> observer) { meshObserver_ = std::move(observer); }
	void SetAnimationObserver(std::function<void(const std::string& animPath)> observer) { animObserver_ = std::move(observer); }

private:
	AnimatedMeshCache() = default;
	~AnimatedMeshCache() = default;
	AnimatedMeshCache(const AnimatedMeshCache&) = delete;
	AnimatedMeshCache& operator=(const AnimatedMeshCache&) = delete;

	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::shared_ptr<AnimatedMeshData>> meshes_;
	std::unordered_map<std::string, std::shared_ptr<const Animation>> animations_;
	std::function<void(const std::string&, const std::string&)> meshObserver_;
	std::function<void(const std::string&)> animObserver_;
};

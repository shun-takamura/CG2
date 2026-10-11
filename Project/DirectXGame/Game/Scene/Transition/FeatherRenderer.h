#pragma once
#include "Camera.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <vector>

class DirectXCore;
class SRVManager;

/// <summary>
/// トランジション用の羽根の描画。羽根は細かく分割した板を頂点シェーダで曲げて作り（反り・丸み）、
/// 全部の羽根を1つのバッファに入れてインスタンス描画で1回にまとめて描く。
/// 自前の仮想カメラ（エンジンの Camera）を持ち、シーンのカメラとは関係なく向きを動かせる。
/// エンジンのパーティクル（EffectManager）はシーンのカメラに依存し、シーン切り替えで止まるので使わない。
/// 描き先（シーン RT）とビューポートは呼んだ側が貼っておく（ScreenWaterSurface::BeginDraw の後など）。
/// </summary>
class FeatherRenderer {
public:
	static constexpr uint32_t kMaxFeathers = 2048;
	static constexpr uint32_t kTextureCount = 5;   // Resources/Textures/Effect/Feathers_00〜04.dds

	/// <summary>羽根1枚（呼んだ側が毎フレーム動かして渡す）</summary>
	struct Feather {
		Vector3 position{};          // 仮想カメラの空間の位置
		Vector3 rotation{};          // オイラー角（エンジンの MakeAffineMatrix と同じ順）
		float size = 0.4f;           // 一辺の長さ（羽根の長さ）
		float bend = 0.25f;          // 羽軸に沿った反り（板の長さに対する膨らみ）
		float cup = 0.1f;            // 横の丸み
		float alpha = 1.0f;          // 不透明度
		float dissolve = 0.0f;       // ディゾルブ（0 = なし、1 = 消えた）
		float brightness = 1.0f;     // 明るさ（奥の羽根を少し暗くする等）
		uint32_t texture = 0;        // 0〜kTextureCount-1
		float noiseOffset = 0.0f;    // ディゾルブの欠け方を羽根ごとに変える
	};

	struct Settings {
		Vector3 lightDirection = { 0.3f, -0.8f, 0.5f }; // 光の進む向き（左上手前から）
		float ambient = 0.55f;                         // 光の当たらない面の明るさ
		float translucency = 0.35f;                    // 裏から光が透ける強さ
		Vector3 edgeColor = { 0.85f, 0.95f, 1.0f };    // ディゾルブの縁の色
		float edgeWidth = 0.08f;                       // ディゾルブの縁の幅
		float noiseScale = 1.5f;                       // ディゾルブのノイズの細かさ

		// JSON との行き来（TransitionTuning.h）。項目を足したらここにも足す
		template <class V>
		void Visit(V& v) {
			v("lightDirection", lightDirection);
			v("ambient", ambient);
			v("translucency", translucency);
			v("edgeColor", edgeColor);
			v("edgeWidth", edgeWidth);
			v("noiseScale", noiseScale);
		}
	};

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
	void Finalize();

	/// <summary>
	/// 羽根を奥から順に並べ替えてまとめて描く（α ブレンド・深度なし）。
	/// 描き先とビューポートは呼んだ側が貼っておくこと
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* cmd, const std::vector<Feather>& feathers, float time);

	/// <summary>仮想カメラ（位置・向き・画角を変えてから Draw 前に Update する）</summary>
	Camera& GetCamera() { return camera_; }
	const Camera& GetCamera() const { return camera_; }

	Settings& GetSettings() { return settings_; }
	const Settings& GetSettings() const { return settings_; }

	/// <summary>ImGui で調整（呼んだ側の窓の中に並べる）</summary>
	void OnImGui();

private:
	// Feather.hlsli の FeatherInstance と 1:1（96 バイト）
	struct InstanceForGPU {
		Matrix4x4 world;
		float bend;
		float cup;
		float alpha;
		float dissolve;
		float texture;
		float brightness;
		float noiseOffset;
		float padding;
	};
	// Feather.hlsli の FeatherCamera と 1:1（128 バイト）
	struct CameraForGPU {
		Matrix4x4 viewProjection;
		Vector3 cameraPosition;
		float time;
		Vector3 lightDirection;
		float ambient;
		Vector3 edgeColor;
		float edgeWidth;
		float translucency;
		float noiseScale;
		float padding[2];
	};

	void CreateMesh();
	void CreateRootSignature();
	void CreatePipelineState();

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	// UV の格子（全インスタンスで使い回す）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
	uint32_t indexCount_ = 0;

	// インスタンス（毎フレーム書き換える。フレームの終わりに GPU 完了を待つので 1 本でよい）
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer_;
	InstanceForGPU* instanceData_ = nullptr;
	uint32_t instanceSrvIndex_ = UINT32_MAX;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraForGPU* cameraData_ = nullptr;

	Camera camera_;
	Settings settings_;

	// 並べ替え用（毎フレーム確保しない）
	std::vector<uint32_t> order_;
	std::vector<float> depth_;
};

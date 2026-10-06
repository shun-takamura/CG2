#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <array>
#include <cstdint>
#include "Vector2.h"
#include "Vector3.h"

class DirectXCore;
class SRVManager;

/// <summary>
/// 水面の波の GPU シミュレーション（2次元の波動方程式を高さマップで解く）。
/// 中心まわりの simSize 四方を resolution² のテクスチャで持ち、コンピュートシェーダが固定刻みで
///   h_next = (2h - h_prev + k·∇²h) · damping   （k = (c·Δt/Δx)²）
/// を進める。外から入れた波源（AddImpulse）はガウス形のへこみとして足す。
/// 波の数に上限が無く、重なれば干渉し、減衰しながら広がって範囲の縁で吸収される。
/// テクスチャは R=今の高さ / G=1つ前の高さ（R32G32_FLOAT）を 2 枚で交互に読み書きする。
/// WaterSurface が持ち、PS（t6）で高さの傾きを法線に足す。
/// </summary>
class RippleSimulation {
public:
	static constexpr uint32_t kResolution = 512;
	static constexpr uint32_t kMaxImpulsesPerFrame = 32;

	struct Settings {
		float simSize = 40.0f;          // シミュレーションする範囲の一辺 [m]
		float waveSpeed = 1.5f;         // 波の伝わる速さ [m/s]
		float damping = 0.985f;         // 1刻みごとの減衰（1 で減衰なし）
		float stepsPerSecond = 60.0f;   // 固定刻みの回数
		float edgeFade = 0.08f;         // 範囲の縁で波を吸収する幅（範囲に対する割合）
		float normalScale = 1.0f;       // 高さの傾きを法線に足す倍率
	};

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

	/// <summary>時間を進める（実際の計算は Dispatch でまとめて行う）</summary>
	void Update(float deltaTime);

	/// <summary>
	/// 波源を入れる（その位置を中心にガウス形にへこませる）。1フレーム kMaxImpulsesPerFrame 個まで。
	/// </summary>
	/// <param name="radius">へこみの半径 [m]</param>
	/// <param name="strength">へこみの深さ [m] 相当（正でへこむ）</param>
	void AddImpulse(const Vector3& position, float radius, float strength);

	/// <summary>
	/// 溜まった刻みの数だけコンピュートシェーダを回す。シーンの描画の最初（SRV ヒープ設定後）に呼ぶ。
	/// グラフィックスのルートシグネチャ・PSO は呼んだ側で設定し直すこと。
	/// </summary>
	void Dispatch();

	/// <summary>最新の高さマップ（PS 読み取り可能な状態）</summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetHeightSrvHandle() const;

	void SetCenter(const Vector2& centerXZ) { center_ = centerXZ; }
	const Vector2& GetCenter() const { return center_; }
	Settings& GetSettings() { return settings_; }
	const Settings& GetSettings() const { return settings_; }

	/// <summary>水面を平らに戻す（次の Dispatch で消す）</summary>
	void Clear() { clearRequested_ = true; }

private:
	// RippleSimulation.CS.hlsl の SimParams と 1:1
	struct Impulse {
		Vector2 uv;        // 範囲内の UV
		float radius;      // UV 単位
		float strength;
	};
	struct SimParamsForGPU {
		float waveFactor;        // k = (c·Δt/Δx)²（安定のため 0.5 未満）
		float damping;
		float edgeFade;
		uint32_t impulseCount;
		uint32_t resolution;
		uint32_t clear;          // 1 なら高さを 0 にする
		float padding[2];
		Impulse impulses[kMaxImpulsesPerFrame];
	};

	void CreateTextures();
	void CreateRootSignature();
	void CreatePipelineState();
	void Transition(uint32_t index, D3D12_RESOURCE_STATES after);

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> textures_;
	std::array<D3D12_RESOURCE_STATES, 2> states_{};
	std::array<uint32_t, 2> srvIndices_{};
	std::array<uint32_t, 2> uavIndices_{};
	uint32_t current_ = 0;              // 最新の高さが入っている方

	// 刻みごとに中身を変えるので、1フレームの最大刻み数ぶん CB を持つ（GPU が読み終わる前に上書きしない）
	static constexpr uint32_t kMaxStepsPerFrame = 4;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxStepsPerFrame> paramsResources_;
	std::array<SimParamsForGPU*, kMaxStepsPerFrame> paramsData_{};

	Settings settings_;
	Vector2 center_{};
	float accumulator_ = 0.0f;
	bool clearRequested_ = true;        // 生成直後のテクスチャの中身は保証されないので最初に消す
	uint32_t impulseCount_ = 0;
	std::array<Impulse, kMaxImpulsesPerFrame> pendingImpulses_{};
};

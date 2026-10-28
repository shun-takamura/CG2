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
///
/// 既定では中心を動かすと波も一緒に動く（ガラス球の液面のように、器にくっついた水）。
/// SetScrollWithCenter(true) にすると波はワールドに残り、範囲だけが中心について動く（低空の航跡。13_RiverWater.md §8）。
/// その時の範囲の中心はテクセルの格子にそろえ、動いた分だけ中身をずらす（新しく入る所は平らな水）。
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

	~RippleSimulation();

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
	/// <summary>
	/// 高さマップが表す範囲の中心。PS はこれで UV を出すこと。
	/// スクロールが無効なら SetCenter の値、有効なら Dispatch でテクセルの格子にそろえた値
	/// </summary>
	const Vector2& GetCenter() const { return scrollWithCenter_ ? gridCenter_ : center_; }

	/// <summary>
	/// true で波をワールドに残し、範囲だけを中心について動かす（既定 false＝波も中心と一緒に動く）。
	/// 切り替えた時は水面を平らに戻す
	/// </summary>
	void SetScrollWithCenter(bool enabled);
	bool IsScrollWithCenter() const { return scrollWithCenter_; }
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
		int32_t shift[2];        // 中身をずらすテクセル数（範囲の中心が動いた分。スクロール時の最初の刻みだけ）
		Impulse impulses[kMaxImpulsesPerFrame];
	};

	void CreateTextures();
	// 確保した SRV/UAV の番号を返す（シーンを作るたびに作られるので、返さないと 512 枠を食い潰す）
	void FreeDescriptors();
	void CreateRootSignature();
	void CreatePipelineState();
	void Transition(uint32_t index, D3D12_RESOURCE_STATES after);

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> textures_;
	std::array<D3D12_RESOURCE_STATES, 2> states_{};
	std::array<uint32_t, 2> srvIndices_{ UINT32_MAX, UINT32_MAX };
	std::array<uint32_t, 2> uavIndices_{ UINT32_MAX, UINT32_MAX };
	uint32_t current_ = 0;              // 最新の高さが入っている方

	// 刻みごとに中身を変えるので、1フレームの最大刻み数ぶん CB を持つ（GPU が読み終わる前に上書きしない）
	static constexpr uint32_t kMaxStepsPerFrame = 4;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxStepsPerFrame> paramsResources_;
	std::array<SimParamsForGPU*, kMaxStepsPerFrame> paramsData_{};

	/// <summary>溜まった波源（ワールド XZ）を、今の範囲の UV に直して刻みの CB に詰める。返り値は詰めた数</summary>
	uint32_t BuildImpulses(Impulse* out) const;

	// AddImpulse で溜める波源。UV にするのは Dispatch（スクロールで範囲が動いた後）
	struct PendingImpulse {
		Vector2 positionXZ;
		float radius;      // [m]
		float strength;
	};

	Settings settings_;
	Vector2 center_{};
	float accumulator_ = 0.0f;
	bool clearRequested_ = true;        // 生成直後のテクスチャの中身は保証されないので最初に消す
	uint32_t impulseCount_ = 0;
	std::array<PendingImpulse, kMaxImpulsesPerFrame> pendingImpulses_{};

	// スクロール（SetScrollWithCenter）
	bool scrollWithCenter_ = false;
	Vector2 gridCenter_{};              // 高さマップが表す範囲の中心（テクセルの格子にそろえた値）
};

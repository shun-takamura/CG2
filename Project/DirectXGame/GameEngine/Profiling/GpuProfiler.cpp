#include "GpuProfiler.h"

#include <d3d12.h>

#include "Profiler.h"

GpuProfiler& GpuProfiler::Instance() {
    static GpuProfiler instance;
    return instance;
}

GpuProfiler::~GpuProfiler() = default;

void GpuProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {
    if (initialized_ || !device || !commandQueue) {
        return;
    }

    // GPU の tick→ms 変換係数（タイムスタンプ周波数はキューから取得）
    UINT64 freq = 0;
    if (FAILED(commandQueue->GetTimestampFrequency(&freq)) || freq == 0) {
        return;  // タイムスタンプ非対応キューなら計測しない
    }
    gpuTickToMs_ = 1000.0 / static_cast<double>(freq);

    // タイムスタンプ用クエリヒープ
    D3D12_QUERY_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kMaxSlots;
    heapDesc.NodeMask = 0;
    if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_)))) {
        return;
    }

    // 解決先の readback バッファ（CPU から読める。常に COPY_DEST）
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(UINT64) * kMaxSlots;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)))) {
        return;
    }

    stack_.reserve(64);
    done_.reserve(64);
    initialized_ = true;
}

void GpuProfiler::BeginScope(ID3D12GraphicsCommandList* commandList, const char* name) {
    if (!initialized_ || !commandList) {
        return;
    }
    // 残り枠が2未満なら計測せず、スタックの釣り合いだけ取る（EndScope で pop される）
    if (slotCount_ + 2 > kMaxSlots) {
        stack_.push_back({ name, kInvalidSlot });
        return;
    }
    const uint32_t beginSlot = slotCount_++;
    commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, beginSlot);
    stack_.push_back({ name, beginSlot });
}

void GpuProfiler::EndScope(ID3D12GraphicsCommandList* commandList) {
    if (!initialized_ || stack_.empty()) {
        return;
    }
    const Pending p = stack_.back();
    stack_.pop_back();
    if (p.beginSlot == kInvalidSlot || !commandList) {
        return;  // 枠不足でスキップした区間
    }
    const uint32_t endSlot = slotCount_++;
    commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endSlot);
    done_.push_back({ p.name, p.beginSlot, endSlot });
}

void GpuProfiler::ResolveTimestamps(ID3D12GraphicsCommandList* commandList) {
    if (!initialized_ || !commandList || slotCount_ == 0) {
        return;
    }
    commandList->ResolveQueryData(
        queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, slotCount_,
        readback_.Get(), 0);
}

void GpuProfiler::ReadbackAndReport() {
    if (!initialized_) {
        return;
    }
    if (slotCount_ > 0 && !done_.empty()) {
        // 解決済みのこのフレーム分だけ読む
        D3D12_RANGE readRange{ 0, sizeof(UINT64) * slotCount_ };
        void* mapped = nullptr;
        if (SUCCEEDED(readback_->Map(0, &readRange, &mapped)) && mapped) {
            const UINT64* ticks = static_cast<const UINT64*>(mapped);
            for (const auto& d : done_) {
                const UINT64 begin = ticks[d.beginSlot];
                const UINT64 end = ticks[d.endSlot];
                const double ms =
                    (end > begin) ? static_cast<double>(end - begin) * gpuTickToMs_ : 0.0;
                Profiler::Instance().AddGpuMs(d.name.c_str(), ms);
            }
            const D3D12_RANGE writeRange{ 0, 0 };  // CPU は書いていない
            readback_->Unmap(0, &writeRange);
        }
    }

    // 次フレーム用にリセット
    slotCount_ = 0;
    stack_.clear();
    done_.clear();
}

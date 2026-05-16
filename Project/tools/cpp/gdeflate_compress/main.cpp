// GDeflate compressor CLI
//
// 用途:
//   pack_assets.py から呼ばれてアセットを GDeflate 圧縮する。
//   DirectStorage SDK 同梱の IDStorageCompressionCodec をそのまま使うので
//   ランタイム解凍 (DSTORAGE_COMPRESSION_FORMAT_GDEFLATE) と完全互換。
//
// 使い方:
//   gdeflate_compress.exe <input_file> <output_file>
//     入力ファイル全体を GDeflate 圧縮し、生バイト列のみを出力ファイルに書く。
//     ヘッダー等は付けない (元サイズは pack 側の index に格納するため不要)。
//   gdeflate_compress.exe --self-test
//     簡単な round-trip テストを実行 (CompressBuffer → DecompressBuffer)。
//
// 終了コード: 0 = 成功 / 1 = 失敗

#include <Windows.h>
#include <dstorage.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size < 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good() || f.eof();
}

bool WriteFile(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

int Compress(const std::string& inPath, const std::string& outPath) {
    std::vector<uint8_t> src;
    if (!ReadFile(inPath, src)) {
        std::fprintf(stderr, "[gdeflate] failed to read input: %s\n", inPath.c_str());
        return 1;
    }

    ComPtr<IDStorageCompressionCodec> codec;
    HRESULT hr = DStorageCreateCompressionCodec(
        DSTORAGE_COMPRESSION_FORMAT_GDEFLATE,
        0,  // numThreads = 0 → 自動 (論理コア数)
        IID_PPV_ARGS(&codec));
    if (FAILED(hr)) {
        std::fprintf(stderr, "[gdeflate] DStorageCreateCompressionCodec failed: 0x%08X\n",
                     static_cast<unsigned>(hr));
        return 1;
    }

    const size_t bound = codec->CompressBufferBound(src.size());
    std::vector<uint8_t> dst(bound);
    size_t actual = 0;
    hr = codec->CompressBuffer(
        src.data(), src.size(),
        DSTORAGE_COMPRESSION_BEST_RATIO,
        dst.data(), dst.size(), &actual);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[gdeflate] CompressBuffer failed: 0x%08X\n",
                     static_cast<unsigned>(hr));
        return 1;
    }

    if (!WriteFile(outPath, dst.data(), actual)) {
        std::fprintf(stderr, "[gdeflate] failed to write output: %s\n", outPath.c_str());
        return 1;
    }
    return 0;
}

int SelfTest() {
    std::vector<uint8_t> src(64 * 1024);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i & 0xFF);

    ComPtr<IDStorageCompressionCodec> codec;
    HRESULT hr = DStorageCreateCompressionCodec(
        DSTORAGE_COMPRESSION_FORMAT_GDEFLATE, 0, IID_PPV_ARGS(&codec));
    if (FAILED(hr)) return 1;

    std::vector<uint8_t> comp(codec->CompressBufferBound(src.size()));
    size_t compSize = 0;
    if (FAILED(codec->CompressBuffer(src.data(), src.size(),
        DSTORAGE_COMPRESSION_BEST_RATIO, comp.data(), comp.size(), &compSize))) return 1;

    std::vector<uint8_t> back(src.size());
    size_t backSize = 0;
    if (FAILED(codec->DecompressBuffer(comp.data(), compSize,
        back.data(), back.size(), &backSize))) return 1;

    if (backSize != src.size() || std::memcmp(src.data(), back.data(), src.size()) != 0) {
        std::fprintf(stderr, "[gdeflate] self-test mismatch\n");
        return 1;
    }
    std::printf("[gdeflate] self-test OK (in=%zu out=%zu)\n", src.size(), compSize);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return SelfTest();
    }
    if (argc != 3) {
        std::fprintf(stderr,
            "usage: gdeflate_compress <input_file> <output_file>\n"
            "       gdeflate_compress --self-test\n");
        return 1;
    }
    return Compress(argv[1], argv[2]);
}

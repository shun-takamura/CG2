#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include <d3d12.h>

class DirectXCore;
class SRVManager;

// 単一フォント・単一ピクセル高のグリフを R8 アトラスに焼く。
// ASCII+仮名はプリロード、他はオンデマンドで bake する。
class FontAtlas {
public:
	struct GlyphInfo {
		float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // アトラス上の UV
		int   width = 0, height = 0;                       // ピクセル単位のグリフサイズ
		int   xoff = 0, yoff = 0;                          // ベースライン基準のオフセット
		float advance = 0.0f;                              // 次グリフへの進み（ピクセル）
		bool  valid = false;
	};

	/// <summary>
	/// TTF を読み込んでフォントの寸法を求め、R8_UNORM のアトラステクスチャ（DEFAULT ヒープ）と SRV を作る
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">アトラスの SRV を確保する SRVManager</param>
	/// <param name="ttfPath">AssetLocator::Open で読める形式のフォントのパス（例 "Resources/Fonts/MPLUS1p-Medium.ttf"）</param>
	/// <param name="pixelHeight">bake 解像度（グリフの高さ [px]）</param>
	/// <param name="atlasSize">テクスチャ一辺の画素数（正方）</param>
	/// <returns>成功なら true。フォントの読み込み・解析・テクスチャ作成に失敗したら false</returns>
	bool Initialize(DirectXCore* dxCore, SRVManager* srvManager,
		const std::string& ttfPath, int pixelHeight, int atlasSize);

	/// <summary>SRV を返し、アトラス・フォント情報・bake 待ち・グリフ表を解放する</summary>
	void Finalize();

	/// <summary>グリフを取得（未 bake なら距離フィールドを bake して bake 待ちに積んでから返す）</summary>
	/// <param name="codepoint">Unicode のコードポイント</param>
	/// <returns>グリフ情報。フォントに無い・アトラスが一杯なら valid=false（空白は valid で大きさ 0）</returns>
	const GlyphInfo& GetOrBake(uint32_t codepoint);

	/// <summary>蓄積した bake を UPLOAD バッファ経由でアトラステクスチャに反映する（COPY_DEST に遷移する）。フレーム描画前に呼ぶ</summary>
	/// <param name="cmd">コピーを積むコマンドリスト</param>
	void FlushPendingUploads(ID3D12GraphicsCommandList* cmd);

	/// <summary>アトラスを PIXEL_SHADER_RESOURCE 状態に置く（描画前バリア用）</summary>
	/// <param name="cmd">バリアを積むコマンドリスト</param>
	void EnsureShaderResourceState(ID3D12GraphicsCommandList* cmd);

	/// <summary>アトラスの SRV インデックスを返す</summary>
	/// <returns>SRV インデックス</returns>
	uint32_t GetSrvIndex() const { return srvIndex_; }
	/// <summary>bake 解像度を返す</summary>
	/// <returns>グリフの高さ [px]</returns>
	int      GetPixelHeight() const { return pixelHeight_; }
	/// <summary>アトラスの一辺を返す</summary>
	/// <returns>画素数</returns>
	int      GetAtlasSize() const { return atlasSize_; }
	/// <summary>ベースラインから上端までの高さを返す</summary>
	/// <returns>アセント [px]</returns>
	float    GetAscent() const { return scaledAscent_; }
	/// <summary>ベースラインから下端までを返す（下向きなので負）</summary>
	/// <returns>ディセント [px]</returns>
	float    GetDescent() const { return scaledDescent_; }
	/// <summary>行間の追加分を返す</summary>
	/// <returns>ラインギャップ [px]</returns>
	float    GetLineGap() const { return scaledLineGap_; }
	/// <summary>1 行ぶん下へ進む量を返す</summary>
	/// <returns>アセント − ディセント ＋ ラインギャップ [px]</returns>
	float    GetLineAdvance() const { return scaledAscent_ - scaledDescent_ + scaledLineGap_; }

	/// <summary>
	/// 既定文字セット（ASCII・ひらがな・カタカナ・よく使う全角記号）を bake して bake 待ちに積む。
	/// テクスチャへの反映は呼び出し側の FlushPendingUploads で行う
	/// </summary>
	/// <param name="cmd">未使用（nullptr でよい）</param>
	void PreloadDefaultCharset(ID3D12GraphicsCommandList* cmd);

private:
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	std::vector<uint8_t> ttfData_; // TTF 全データ
	struct FontInfoStorage;
	FontInfoStorage* fontInfo_ = nullptr; // pImpl 風（stb_truetype 型を隠す）

	int pixelHeight_ = 32;
	int atlasSize_ = 1024;
	float fontScale_ = 1.0f;
	float scaledAscent_ = 0.0f;
	float scaledDescent_ = 0.0f;
	float scaledLineGap_ = 0.0f;

	// シェルフパッキング用カーソル
	int shelfX_ = 1; // 0 番目の列は黒画素として空けておく（UV 端の補間防止）
	int shelfY_ = 1;
	int shelfHeight_ = 0;
	static constexpr int kShelfPadding = 1; // 隣接グリフのにじみ防止

	Microsoft::WRL::ComPtr<ID3D12Resource> atlasResource_;
	uint32_t srvIndex_ = 0;
	D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;

	// bake 待ち
	struct PendingUpload {
		int x = 0, y = 0, w = 0, h = 0;
		std::vector<uint8_t> bytes; // R8 ピクセル
	};
	std::vector<PendingUpload> pending_;

	std::unordered_map<uint32_t, GlyphInfo> glyphs_;

	/// <summary>
	/// グリフの距離フィールド（SDF）を作り、シェルフパッキングでアトラス上の場所を決めて bake 待ちに積む
	/// </summary>
	/// <param name="codepoint">Unicode のコードポイント</param>
	/// <param name="out">求めた UV・大きさ・オフセット・送り幅を書き込む先</param>
	/// <returns>書けたら true。フォント未読み込み・SDF 作成失敗・アトラスが一杯なら false</returns>
	bool BakeGlyph(uint32_t codepoint, GlyphInfo& out);
	/// <summary>アトラスのリソース状態を遷移する（今と同じなら何もしない）</summary>
	/// <param name="cmd">バリアを積むコマンドリスト</param>
	/// <param name="newState">遷移先の状態</param>
	void TransitionTo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState);
};

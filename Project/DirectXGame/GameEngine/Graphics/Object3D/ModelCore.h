#pragma once
#include"DirectXCore.h"

class ModelCore
{
	DirectXCore* dxCore_;

public:

	/// <summary>DirectXCore を返す</summary>
	/// <returns>Initialize で受け取った DirectXCore</returns>
	DirectXCore* GetDXCore()const { return dxCore_; }

	/// <summary>モデル読み込みで使う DirectXCore を覚える</summary>
	/// <param name="dxCore">DirectXCore</param>
	void Initialize(DirectXCore* dxCore);

};


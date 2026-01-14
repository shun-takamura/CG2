#pragma once
#include"DirectXCore.h"

class ModelManager
{
	DirectXCore* dxCore_;

public:

	// ゲッターロボ
	DirectXCore* GetDXCore()const { return dxCore_; }

	void Initialize(DirectXCore* dxCore);

};


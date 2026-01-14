#pragma once
#include"DirectXCore.h"

class ModelCore
{
	DirectXCore* dxCore_;

public:

	// ゲッターロボ
	DirectXCore* GetDXCore()const { return dxCore_; }

	void Initialize(DirectXCore* dxCore);

};


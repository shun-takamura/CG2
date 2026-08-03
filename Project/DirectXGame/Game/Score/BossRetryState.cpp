#include "BossRetryState.h"

BossRetryState* BossRetryState::GetInstance()
{
	static BossRetryState instance;
	return &instance;
}

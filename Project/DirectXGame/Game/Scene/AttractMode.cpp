#include "AttractMode.h"

AttractMode* AttractMode::GetInstance() {
	static AttractMode instance;
	return &instance;
}

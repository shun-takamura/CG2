#include "Gameplay.h"
#include <unordered_map>

namespace {
	// エンティティポインタ → ゲームプレイコンポーネント。
	// 破棄時に Remove で除去するため dangling/ポインタ再利用のエイリアスは起きない。
	std::unordered_map<const IImGuiEditable*, GameplayComponents> g_store;
}

namespace Gameplay {

	GameplayComponents& Of(const IImGuiEditable* e) {
		return g_store[e];
	}

	void Remove(const IImGuiEditable* e) {
		g_store.erase(e);
	}

	void Clear() {
		g_store.clear();
	}

} // namespace Gameplay

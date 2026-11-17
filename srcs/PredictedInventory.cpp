#include "PredictedInventory.hpp"

std::uint32_t PredictedInventory::apply(const InventoryAction& action) {
	applyAction(_view, action);
	const std::uint32_t seq = _nextSeq++;
	_pending.push_back({seq, action});
	return seq;
}

void PredictedInventory::onServerState(const Inventory& server, const std::uint32_t ackSeq) {
	_view = server;
	while (!_pending.empty() && _pending.front().seq <= ackSeq)
		_pending.pop_front();
	for (const Pending& pending : _pending)
		applyAction(_view, pending.action);
}

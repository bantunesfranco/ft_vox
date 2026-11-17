#ifndef PREDICTED_INVENTORY_HPP
#define PREDICTED_INVENTORY_HPP

#include <cstdint>
#include <deque>

#include "Inventory.hpp"

// The client's view of its inventory: actions apply at once and stay pending until the
// server acknowledges them, so a late or contradicting server state can be replayed over.
class PredictedInventory {
public:
	[[nodiscard]] const Inventory& view() const noexcept { return _view; }

	// Applies to the view, records it as pending, returns its sequence number (the first is 1).
	std::uint32_t apply(const InventoryAction& action);
	// The view becomes `server`; pending actions up to ackSeq are dropped, the rest re-applied in order.
	void onServerState(const Inventory& server, std::uint32_t ackSeq);

private:
	struct Pending {
		std::uint32_t seq;
		InventoryAction action;
	};

	Inventory _view;
	std::deque<Pending> _pending;
	std::uint32_t _nextSeq = 1;
};

#endif

#ifndef UI_HPP
#define UI_HPP

#include <array>
#include <string_view>
#include <vector>

#include "GameMode.hpp"
#include "ItemId.hpp"

enum class UiActionType {
	START_GAME,
	RESUME_GAME,
	RETURN_TO_TITLE,
	QUIT_GAME,
	SET_WIREFRAME,
	SET_CLEAR_COLOR,
	INVENTORY_SLOT_CLICK,
};

struct UiAction {
	UiActionType type;
	bool enabled{false};
	std::array<float, 4> color{};
	int slot{-1};
	bool secondaryClick{false};
};

struct UiItemStack {
	ItemId item{ItemId::Empty};
	std::uint16_t count{0};
	std::string_view name{};

	[[nodiscard]] bool empty() const noexcept { return count == 0 || item == ItemId::Empty; }
};

struct UiFrameModel {
	GameState state{GameState::START};
	int selectedHotbarSlot{0};
	int fps{0};
	std::array<float, 3> cameraPosition{};
	bool showDebug{true};
	bool wireframe{false};
	std::array<float, 4> clearColor{};
	std::array<UiItemStack, 36> inventorySlots{};
	UiItemStack cursorStack{};
};

class UI {
public:
	virtual ~UI() = default;
	virtual void beginFrame() = 0;
	virtual std::vector<UiAction> render(const UiFrameModel& model) = 0;
	virtual void endFrame() = 0;
	virtual bool wantsMouseCapture() const = 0;
	virtual bool wantsKeyboardCapture() const = 0;
	virtual void shutdown() = 0;
};

#endif

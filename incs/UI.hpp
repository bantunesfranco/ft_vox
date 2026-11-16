#ifndef UI_HPP
#define UI_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "GameMode.hpp"
#include "ItemId.hpp"

enum class UiActionType {
	START_GAME,
	START_GAME_MP,
	HOST_MP,
	JOIN_MP,
	RESUME_GAME,
	RETURN_TO_TITLE,
	QUIT_GAME,
	SET_WIREFRAME,
	SET_CLEAR_COLOR,
	INVENTORY_SLOT_CLICK,
};

struct UiAction {
	UiActionType type{};
	bool enabled{false};
	std::array<float, 4> color{};
	int slot{-1};
	bool secondaryClick{false};
	std::string address{};      // JOIN_MP: "host" or "host:port"
	std::string playerName{};   // HOST_MP / JOIN_MP
};

struct UiItemStack {
	ItemId item{ItemId::Empty};
	std::uint16_t count{0};
	std::string_view name{};

	[[nodiscard]] bool empty() const noexcept { return count == 0 || item == ItemId::Empty; }
};

// A name drawn over another player's head. Position in normalized device
// coordinates (-1..1, +y up), so the UI maps it to its own pixel size.
struct UiNameTag {
	std::string name;
	float ndcX{0.0f};
	float ndcY{0.0f};
	float distance{0.0f};     // blocks from the camera, for fading
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
	std::string statusText{};                 // multiplayer: "Connecting...", shown centered
	std::vector<std::string> players{};       // multiplayer: other players, for the debug window
	std::string lastError{};                  // why the last multiplayer attempt ended
	std::vector<UiNameTag> nameTags{};        // multiplayer: visible players, far to near
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

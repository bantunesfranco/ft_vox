#ifndef UI_HPP
#define UI_HPP

#include <array>
#include <vector>

#include "GameMode.hpp"
#include "glm/glm.hpp"

enum class UiActionType {
	START_GAME,
	RESUME_GAME,
	RETURN_TO_TITLE,
	QUIT_GAME,
	SET_WIREFRAME,
	SET_CLEAR_COLOR,
};

struct UiAction {
	UiActionType type;
	bool enabled{false};
	std::array<float, 4> color{};
};

struct UiFrameModel {
	GameState state{GameState::START};
	int selectedHotbarSlot{0};
	int fps{0};
	glm::vec3 cameraPosition{};
	bool showDebug{true};
	bool wireframe{false};
	std::array<float, 4> clearColor{};
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

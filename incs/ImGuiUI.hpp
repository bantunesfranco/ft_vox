#ifndef IMGUI_UI_HPP
#define IMGUI_UI_HPP

#include <memory>

#include "UI.hpp"

struct GLFWwindow;

class ImGuiUI final : public UI {
public:
	explicit ImGuiUI(GLFWwindow* window);
	~ImGuiUI() override;
	ImGuiUI(const ImGuiUI&) = delete;
	ImGuiUI& operator=(const ImGuiUI&) = delete;

	void beginFrame() override;
	std::vector<UiAction> render(const UiFrameModel& model) override;
	void endFrame() override;
	bool wantsMouseCapture() const override;
	bool wantsKeyboardCapture() const override;
	void shutdown() override;

private:
	bool initialized{false};
};

std::unique_ptr<UI> createImGuiUI(GLFWwindow* window);

#endif

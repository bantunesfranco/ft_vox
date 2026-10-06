#ifndef IMGUI_UI_HPP
#define IMGUI_UI_HPP

#include <memory>

#include <glad/glad.h>
#include "UI.hpp"

struct GLFWwindow;

class ImGuiUI final : public UI {
public:
	ImGuiUI(GLFWwindow* window, GLuint guiAtlas, GLuint blockAtlas);
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
	GLuint guiAtlas{0};
	GLuint blockAtlas{0};
};

std::unique_ptr<UI> createImGuiUI(GLFWwindow* window, GLuint guiAtlas, GLuint blockAtlas);

#endif

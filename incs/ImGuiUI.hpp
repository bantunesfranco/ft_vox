#ifndef IMGUI_UI_HPP
#define IMGUI_UI_HPP

#include <array>
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
	// Multiplayer form, kept across frames (ImGui::InputText edits these in place).
	std::array<char, 128> joinAddress{"127.0.0.1"};
	std::array<char, 33>  playerName{"Player"};
};

std::unique_ptr<UI> createImGuiUI(GLFWwindow* window, GLuint guiAtlas, GLuint blockAtlas);

#endif

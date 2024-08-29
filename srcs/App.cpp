#include "App.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

App::App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool> settings) : Engine(width, height, title, settings) {

	_showWireframe = false;
	setCallbackFunctions();
	setupImGui(window);
}

void App::setCallbackFunctions(void)
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
	// std::vector<Vertex> vertices = {
	// 	{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	// 	{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	// 	{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	// 	{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
	// };
	while (windowIsOpen(window))
	{
		glfwPollEvents();
		// renderer->render(window, camera, vertices);
		renderImGui(camera, _showWireframe);
		glfwSwapBuffers(window);
		setFrameTime();
	}
}

void App::terminate()
{

	if (ImGui::GetCurrentContext())
	{
		ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Engine::terminate();
}
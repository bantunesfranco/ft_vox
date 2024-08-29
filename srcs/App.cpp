#include "App.hpp"

App::App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool> settings) : Engine(width, height, title, settings) {
	setCallbackFunctions();
}

void App::setCallbackFunctions(void)
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
	std::vector<Vertex> vertices = {
		{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
		{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
		{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
	};
	while (windowIsOpen(window))
	{
		glfwPollEvents();
		renderer->render(window, camera, vertices);
		glfwSwapBuffers(window);
		setFrameTime();
	}
}

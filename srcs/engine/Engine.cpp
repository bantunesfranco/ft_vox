#include "Engine.hpp"

Engine*	_instance = nullptr;

Engine::Engine()
{
	bool init = glfwInit();

	if (!init)
	{
		throw std::runtime_error("Failed to initialize GLFW");
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_MAXIMIZED, _settings[MLX_MAXIMIZED]);
	glfwWindowHint(GLFW_DECORATED, _settings[MLX_DECORATED]);
	glfwWindowHint(GLFW_VISIBLE, !_settings[MLX_HEADLESS]);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif

	glfwWindowHint(GLFW_RESIZABLE, resize);
	this->window = glfwCreateWindow(width, height, title, _settings[SET_FULLSCREEN] ? glfwGetPrimaryMonitor() : NULL, NULL);
	if (!this->window)
	{
		this->terminateEngine();
		throw std::runtime_error("Failed to create window");
	}
}

Engine *Engine::initEngine(int32_t width, int32_t height, const char* title)
{
	ENG_NONNULL(title);
	ENG_ASSERT(width > 0, "Width must be greater than 0");
	ENG_ASSERT(height > 0, "Height must be greater than 0");
	ENG_ASSERT(!_instance, "Engine already initialized");

	try
	{
		_instance = new Engine();
		_instance->window = new InitWindow(width, height, title);
		_instance->renderer = new Renderer();
	}
	catch (const std::runtime_error &e)
	{
		eng_error("Engine init: ", e.what());
		return nullptr;
	}
	return this->_instance;
}
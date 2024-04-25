#include "Engine.hpp"
#include <iostream>

Engine*		Engine::_instance = nullptr;
vox_errno_t	Engine::vox_errno = VOX_SUCCESS;

Engine::Engine()
{
	bool init = glfwInit();

	if (!init)
	{
		throw EngineException(VOX_GLFWFAIL);
	}

	this->window = nullptr;
	this->renderer = nullptr;
	
	for (int i = 0; i < VOX_SETTINGS_MAX; i++)
		this->_settings[i] = false;
	this->_settings[VOX_DECORATED] = true;
}

void	Engine::terminateEngine()
{
	if (this->window)
	{
		glfwDestroyWindow(this->window);
		this->window = nullptr;
	}
	if (this->renderer)
	{
		delete this->renderer;
		this->renderer = nullptr;
	}
	glfwTerminate();
}

void	Engine::setSetting(int32_t setting, bool value)
{
	VOX_ASSERT(setting >= 0 && setting < VOX_SETTINGS_MAX, "Invalid setting");
	this->_settings[setting] = value;
}

void Engine::_initWindow(int32_t width, int32_t height, const char* title, bool resize)
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_MAXIMIZED, this->_settings[VOX_MAXIMIZED]);
	glfwWindowHint(GLFW_DECORATED, this->_settings[VOX_DECORATED]);
	glfwWindowHint(GLFW_VISIBLE, !this->_settings[VOX_HEADLESS]);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif

	glfwWindowHint(GLFW_RESIZABLE, resize);
	this->window = glfwCreateWindow(width, height, title, this->_settings[VOX_FULLSCREEN] ? glfwGetPrimaryMonitor() : NULL, NULL);
	if (!this->window)
	{
		this->terminateEngine();
		throw EngineException(VOX_WINFAIL);
	}
	glfwMakeContextCurrent(this->window);
}

Engine *Engine::initEngine(int32_t width, int32_t height, const char* title, bool resize)
{
	VOX_NONNULL(title);
	VOX_ASSERT(width > 0, "Width must be greater than 0");

	VOX_ASSERT(height > 0, "Height must be greater than 0");
	VOX_ASSERT(!_instance, "Engine already initialized");

	try
	{
		_instance = new Engine();
		_instance->_initWindow(width, height, title, resize);
		_instance->renderer =  new Renderer(_instance);
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		_instance->terminateEngine();
		return nullptr;
	}
	return _instance;
}
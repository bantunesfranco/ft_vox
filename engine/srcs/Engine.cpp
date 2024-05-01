#include "Engine.hpp"
#include <iostream>
#include <glad/gl.h>

Engine*		Engine::_instance = nullptr;
vox_errno_t	Engine::vox_errno = VOX_SUCCESS;

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

Engine::Engine() : window(nullptr), renderer(nullptr)
{
	bool init = glfwInit();

	if (!init)
	{
		throw EngineException(VOX_GLFWFAIL);
	}
	
	for (int i = 0; i < VOX_SETTINGS_MAX; i++)
		_settings[i] = false;
	_settings[VOX_DECORATED] = true;
}

void	Engine::terminateEngine()
{
	if (window)
	{
		glfwDestroyWindow(window);
		window = nullptr;
	}
	if (renderer)
	{
		delete renderer;
		renderer = nullptr;
	}
	glfwTerminate();
}

void	Engine::setSetting(int32_t setting, bool value)
{
	VOX_ASSERT(setting >= 0 && setting < VOX_SETTINGS_MAX, "Invalid setting");
	_settings[setting] = value;
}

void Engine::_initWindow(int32_t width, int32_t height, const char* title, bool resize)
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_MAXIMIZED, _settings[VOX_MAXIMIZED]);
	glfwWindowHint(GLFW_DECORATED, _settings[VOX_DECORATED]);
	glfwWindowHint(GLFW_VISIBLE, !_settings[VOX_HEADLESS]);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif
	glfwWindowHint(GLFW_RESIZABLE, resize);

	_width = width;
	_height = height;
	window = glfwCreateWindow(width, height, title, _settings[VOX_FULLSCREEN] ? glfwGetPrimaryMonitor() : NULL, NULL);
	if (!window)
	{
		terminateEngine();
		throw EngineException(VOX_WINFAIL);
	}

	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);
	glfwSwapInterval(1);
	// glfwSetWindowUserPointer(window, (void*)this->window);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);
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
		_instance->renderer =  new Renderer();
		_instance->renderer->initBuffers();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		_instance->terminateEngine();
		if (_instance)
		{
			delete _instance;
			_instance = nullptr;
		}
	}
	return _instance;
}

void	Engine::run()
{
	while (!glfwWindowShouldClose(window))
	{
		renderer->render();
		glfwSwapBuffers(window);
		glfwPollEvents();
	}
}

void	Engine::closeWindow()
{
	glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void	Engine::setKeyCallback(GLFWkeyfun callback)
{
	glfwSetKeyCallback(window, callback);
}

void	Engine::setMouseButtonCallback(GLFWmousebuttonfun callback)
{
	glfwSetMouseButtonCallback(window, callback);
}

void	Engine::setCursorPosCallback(GLFWcursorposfun callback)
{
	glfwSetCursorPosCallback(window, callback);
}

void	Engine::setScrollCallback(GLFWscrollfun callback)
{
	glfwSetScrollCallback(window, callback);
}

void	Engine::setResizeCallback(GLFWwindowsizefun callback)
{
	glfwSetWindowSizeCallback(window, callback);
}

void	Engine::setFramebufferSizeCallback(GLFWframebuffersizefun callback)
{
	glfwSetFramebufferSizeCallback(window, callback);
}

void	Engine::setWindowCloseCallback(GLFWwindowclosefun callback)
{
	glfwSetWindowCloseCallback(window, callback);
}

void	Engine::setErrorCallback(GLFWerrorfun callback)
{
	glfwSetErrorCallback(callback);
}

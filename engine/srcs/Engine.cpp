#include "Engine.hpp"

#include <iostream>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

vox_errno_t	Engine::vox_errno = VOX_SUCCESS;

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

Engine::Engine(const int32_t width, const int32_t height, const char* title, std::map<settings_t, bool>& settings)
	: window(nullptr), renderer(nullptr), camera(nullptr), settings{0, 0, 0, 1, 0, 0}
{
	if (!glfwInit())
		throw EngineException(VOX_GLFWFAIL);
	_glfwInitialized = true;

	for (auto const& [key, val] : settings)
		setSetting(key, val);

	if (settings[VOX_MAXIMIZED])
		setSetting(VOX_FULLSCREEN, false);

	VOX_NONNULL(title);
	VOX_ASSERT(width > 0, "Width must be greater than 0");
	VOX_ASSERT(height > 0, "Height must be greater than 0");

	try
	{
		initWindow(width, height, title);
		camera = std::make_unique<Camera>(window);
		renderer = std::make_unique<Renderer>();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		Engine::terminate();
		std::exit(vox_errno);
	}
}

Engine::~Engine()
{
	Engine::terminate();
}

void	Engine::terminate()
{
	// GPU resources must be released while the OpenGL context still exists.
	renderer.reset();
	if (window)
	{
		glfwDestroyWindow(window);
		window = nullptr;
	}
	if (_glfwInitialized) {
		glfwTerminate();
		_glfwInitialized = false;
	}
}

void	Engine::setSetting(const int32_t setting, const bool value)
{
	VOX_ASSERT(setting >= 0 && setting < VOX_SETTINGS_MAX, "Invalid setting");
	settings[setting] = value;
}

void Engine::toggleFullscreen()
{
	if (!window)
		return;
	if (!settings[VOX_FULLSCREEN]) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
		if (!mode)
			return;

		glfwGetWindowPos(window, &_windowedX, &_windowedY);
		glfwGetWindowSize(window, &_windowedWidth, &_windowedHeight);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		settings[VOX_FULLSCREEN] = true;
		_width = mode->width;
		_height = mode->height;
		return;
	}

	glfwSetWindowMonitor(
		window,
		nullptr,
		_windowedX,
		_windowedY,
		_windowedWidth,
		_windowedHeight,
		GLFW_DONT_CARE
	);
	settings[VOX_FULLSCREEN] = false;
	_width = _windowedWidth;
	_height = _windowedHeight;
}

void Engine::initWindow(const int32_t width, const int32_t height, const char* title)
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_MAXIMIZED, settings[VOX_MAXIMIZED]);
	glfwWindowHint(GLFW_DECORATED, settings[VOX_DECORATED]);
	glfwWindowHint(GLFW_VISIBLE, !settings[VOX_HEADLESS]);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif
	glfwWindowHint(GLFW_RESIZABLE, settings[VOX_RESIZE]);

	_width = width;
	_height = height;
	_windowedWidth = width;
	_windowedHeight = height;
	window = glfwCreateWindow(width, height, title, settings[VOX_FULLSCREEN] ? glfwGetPrimaryMonitor() : nullptr, nullptr);
	if (!window)
		throw EngineException(VOX_WINFAIL);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	glfwMakeContextCurrent(window);
	gladLoadGL();
	glfwSwapInterval(1);
	glfwSetWindowUserPointer(window, (void*)this);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);

	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_WINFAIL);

}

// Loads an RGBA texture with mipmaps. Throws (and leaks nothing) on failure.
GLuint Engine::loadTexture(const char* path) {
	int width = 0, height = 0, channels = 0;
	unsigned char* data = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
	if (!data)
		throw EngineException(VOX_TEXTFAIL);

	GLuint textureID = 0;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	if (glGetError() != GL_NO_ERROR) {
		glDeleteTextures(1, &textureID);
		throw EngineException(VOX_TEXTFAIL);
	}
	return textureID;
}

void	Engine::toggleWireframe(const bool showWireframe)
{
	if (showWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);  // Switch to wireframe mode
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // Switch to solid fill mode
    }
}

bool Engine::isKeyDown(const keys_t key) const
{
	return window && glfwGetKey(this->window, key) == GLFW_PRESS;
}

bool	Engine::windowIsOpen(GLFWwindow* window)
{
	return window && !glfwWindowShouldClose(window);
}

void	Engine::closeWindow() const
{
	if (window)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void	Engine::setKeyCallback(const GLFWkeyfun callback) const
{
	glfwSetKeyCallback(window, callback);
}

void	Engine::setMouseButtonCallback(const GLFWmousebuttonfun callback) const
{
	glfwSetMouseButtonCallback(window, callback);
}

void	Engine::setCursorPosCallback(const GLFWcursorposfun callback) const
{
	
	glfwSetCursorPosCallback(window, callback);
}

void	Engine::setScrollCallback(const GLFWscrollfun callback) const
{
	glfwSetScrollCallback(window, callback);
}

void	Engine::setResizeCallback(const GLFWwindowsizefun callback) const
{
	glfwSetWindowSizeCallback(window, callback);
}

void	Engine::setFramebufferSizeCallback(const GLFWframebuffersizefun callback) const
{
	glfwSetFramebufferSizeCallback(window, callback);
}

void	Engine::setWindowCloseCallback(const GLFWwindowclosefun callback) const
{
	glfwSetWindowCloseCallback(window, callback);
}

void	Engine::setErrorCallback(const GLFWerrorfun callback) const
{
	glfwSetErrorCallback(callback);
}

FPSCounter::FPSCounter() : _deltaTime(0.0f), _lastTime(static_cast<float>(glfwGetTime())), _frameCount(0), _fps(0) {}

FPSCounter& FPSCounter::getInstance()
{
	if (_instance == nullptr)
		_instance = std::make_unique<FPSCounter>();
	return *_instance;
}

int FPSCounter::getFPS() { return getInstance()._fps; }

float FPSCounter::getDeltaTime() { return getInstance()._deltaTime; }

void FPSCounter::update() {
	const auto currentTime = static_cast<float>(glfwGetTime());

	auto& fpsCounter = getInstance();

	// Time elapsed since last frame
	fpsCounter._deltaTime = currentTime - fpsCounter._lastTime;
	fpsCounter._lastTime = currentTime;

	// Count frames for FPS
	fpsCounter._frameCount++;

	// Update FPS every second
	static float fpsTimer = 0.0f;
	fpsTimer += fpsCounter._deltaTime;
	if (fpsTimer >= 1.0f) {
		fpsCounter._fps = fpsCounter._frameCount;
		fpsCounter._frameCount = 0;
		fpsTimer = 0.0f;
	}
}


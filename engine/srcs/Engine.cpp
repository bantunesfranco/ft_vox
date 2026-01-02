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
	: window(nullptr), renderer(nullptr), camera(nullptr), fpsCounter(nullptr), settings{0, 0, 0, 1, 0, 0}
{
	if (!glfwInit())
		throw EngineException(VOX_GLFWFAIL);

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
		std::exit(Engine::vox_errno);
	}
}

Engine::~Engine()
{
	Engine::terminate();
}

void	Engine::terminate()
{
	if (window)
	{
		glfwDestroyWindow(window);
		window = nullptr;
	}
	glfwTerminate();
}

void	Engine::setSetting(const int32_t setting, const bool value)
{
	VOX_ASSERT(setting >= 0 && setting < VOX_SETTINGS_MAX, "Invalid setting");
	settings[setting] = value;
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
	window = glfwCreateWindow(width, height, title, settings[VOX_FULLSCREEN] ? glfwGetPrimaryMonitor() : nullptr, nullptr);
	if (!window)
		throw EngineException(VOX_WINFAIL);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glfwMakeContextCurrent(window);
	gladLoadGL();
	glfwSwapInterval(1);
	glfwSetWindowUserPointer(window, (void*)this);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);

	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_WINFAIL);

}

GLuint Engine::loadTexture(const char* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_TEXTFAIL);

    // Load texture image here
    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);

    if (!data)
	{
		throw EngineException(VOX_TEXTFAIL);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(data);

	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_TEXTFAIL);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_TEXTFAIL);

    return textureID;
}

GLuint Engine::loadTextureArray(const std::vector<std::string>& paths, int& outWidth, int& outHeight) {
	int width, height, channels;

	stbi_uc* first = stbi_load(
		paths[0].c_str(),
		&width, &height,
		&channels,
		STBI_rgb_alpha
	);

	if (!first)
		throw EngineException(VOX_TEXTFAIL);

	outWidth  = width;
	outHeight = height;

	GLuint texArray;
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texArray);

	glTextureStorage3D(
		texArray,
		1,
		GL_RGBA8,
		width, height,
		static_cast<GLsizei>(paths.size())
	);

	glTextureSubImage3D(
		texArray,
		0,
		0, 0, 0,
		width, height,
		1,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		first
	);

	stbi_image_free(first);

	for (size_t i = 1; i < paths.size(); ++i)
	{
		stbi_uc* data = stbi_load(
			paths[i].c_str(),
			&width, &height,
			&channels,
			STBI_rgb_alpha
		);

		if (!data)
			throw EngineException(VOX_TEXTFAIL);

		assert(width == outWidth && height == outHeight);

		glTextureSubImage3D(
			texArray,
			0,
			0, 0, static_cast<GLint>(i),
			width, height,
			1,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			data
		);

		stbi_image_free(data);
	}

	glTextureParameteri(texArray, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(texArray, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(texArray, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTextureParameteri(texArray, GL_TEXTURE_WRAP_T, GL_REPEAT);

	return texArray;
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
	return glfwGetKey(this->window, key);
}

bool	Engine::windowIsOpen(GLFWwindow* window)
{
	return !glfwWindowShouldClose(window);
}

void	Engine::closeWindow() const
{
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


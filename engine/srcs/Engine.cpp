#include "Engine.hpp"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

vox_errno_t	Engine::vox_errno = VOX_SUCCESS;

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

Engine::Engine(int32_t width, int32_t height, const char* title, std::map<settings_t, bool> settings) : window(nullptr), renderer(nullptr), camera(nullptr), fpsCounter(nullptr), settings{0, 0, 0, 1, 0, 0}
{
	bool init = glfwInit();

	if (!init)
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
		camera = new Camera(window);
		renderer =  new Renderer();
		fpsCounter = new FPSCounter();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		terminate();
		std::exit(Engine::vox_errno);
	}
}

Engine::~Engine()
{
	terminate();
}

void	Engine::terminate()
{
	if (window)
	{
		glfwDestroyWindow(window);
		window = nullptr;
	}
	if (camera)
	{
		delete camera;
		camera = nullptr;
	}
	if (renderer)
	{
		delete renderer;
		renderer = nullptr;
	}
	if (fpsCounter)
	{
		delete fpsCounter;
		fpsCounter = nullptr;
	}
	glfwTerminate();
}

void	Engine::setSetting(int32_t setting, bool value)
{
	VOX_ASSERT(setting >= 0 && setting < VOX_SETTINGS_MAX, "Invalid setting");
	settings[setting] = value;
}

void Engine::initWindow(int32_t width, int32_t height, const char* title)
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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
	window = glfwCreateWindow(width, height, title, settings[VOX_FULLSCREEN] ? glfwGetPrimaryMonitor() : NULL, NULL);
	if (!window)
		throw EngineException(VOX_WINFAIL);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (glGetError() != GL_NO_ERROR)
		throw EngineException(VOX_TEXTFAIL);

    return textureID;
}

void	Engine::toggleWireframe(bool showWireframe)
{
	if (showWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);  // Switch to wireframe mode
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // Switch to solid fill mode
    }
}

bool Engine::isKeyDown(keys_t key){ return glfwGetKey(this->window, key); }

bool	Engine::windowIsOpen(GLFWwindow* window)
{
	return !glfwWindowShouldClose(window);
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

FPSCounter::FPSCounter() : frameCount(0), fps(0)
{
	lastTime = glfwGetTime();
}

void FPSCounter::update()
{
	frameCount++;
	float currentTime = glfwGetTime();
	float deltaTime = currentTime - lastTime;

	if (deltaTime >= 1.0f)
	{
		fps = frameCount;
		frameCount = 0;
		lastTime = currentTime;
	}
}

int FPSCounter::getFPS() const
{
	return fps;
}
#include "Engine.hpp"
#include <iostream>

vox_errno_t	Engine::vox_errno = VOX_SUCCESS;

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

Engine::Engine(int32_t width, int32_t height, const char* title, std::map<settings_t, bool> settings) : window(nullptr), renderer(nullptr), camera(nullptr), settings{0, 0, 0, 1, 0, 0}
{
	bool init = glfwInit();

	if (!init)
	{
		throw EngineException(VOX_GLFWFAIL);
	}

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
}

GLuint Engine::loadTexture(const char* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Load the image
    int width, height, nrChannels;
    unsigned char *data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        uint32_t format;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        std::cerr << "Failed to load texture: " << path << std::endl;
    }
    stbi_image_free(data);
    return textureID;
}


void	Engine::setFrameTime()
{
	_lastFrameTime = glfwGetTime();
}

double Engine::getDeltaTime() {
	double currentFrameTime = glfwGetTime();
	double deltaTime = currentFrameTime - _lastFrameTime;
	_lastFrameTime = currentFrameTime;
	return deltaTime;
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

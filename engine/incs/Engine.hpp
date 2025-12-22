#ifndef ENGINE_HPP
#define ENGINE_HPP
 
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <exception>
#include <map>

#include "Renderer.hpp"
#include "Camera.hpp"
#include "defines.hpp"

#define VOX_ASSERT(val, str) assert(val && str);
#define VOX_NONNULL(val) assert(val && "Value cannot be null");

constexpr double BLOCK_SIZE = 1.0f;

#include <chrono>
#include <memory>

class FPSCounter {
	private:
		float	_deltaTime;
		float	_lastTime;
		int		_frameCount;
		int		_fps;

		static inline std::unique_ptr<FPSCounter> _instance = nullptr;

		FPSCounter& operator=(const FPSCounter&) = default;

	public:
		FPSCounter();
		~FPSCounter() = default;
		FPSCounter(const FPSCounter&) = delete;

		[[nodiscard]] static int getFPS();
		[[nodiscard]] static float getDeltaTime();
		static void update();
		static FPSCounter& getInstance();
};

class Engine
{
	private:
		int32_t	_width{};
		int32_t	_height{};
		double	_lastFrameTime{};

		void	initWindow(int32_t width, int32_t height, const char* title);

	public:
		Engine(int32_t width, int32_t height, const char* title, std::map<settings_t, bool>& settings);
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		virtual ~Engine();

		class EngineException : public std::exception
		{
			public:
				explicit EngineException(vox_errno_t err);
				[[nodiscard]] const char* what() const noexcept override;
		};

		static vox_errno_t			vox_errno;
		GLFWwindow*					window;
		std::unique_ptr<Renderer>	renderer;
		std::unique_ptr<Camera>		camera;
		FPSCounter*					fpsCounter;
		int32_t						settings[VOX_SETTINGS_MAX];
	
		static const char*	vox_strerror(vox_errno_t val);
		void				setSetting(int32_t setting, bool value);

		static bool			windowIsOpen(GLFWwindow* window);
		virtual void		terminate();

		void				setKeyCallback(GLFWkeyfun callback) const;
		void				setMouseButtonCallback(GLFWmousebuttonfun callback) const;
		void				setCursorPosCallback(GLFWcursorposfun callback) const;
		void				setScrollCallback(GLFWscrollfun callback) const;
		void				setResizeCallback(GLFWwindowsizefun callback) const;
		void				setFramebufferSizeCallback(GLFWframebuffersizefun callback) const;
		void				setWindowCloseCallback(GLFWwindowclosefun callback) const;
		void				setErrorCallback(GLFWerrorfun callback) const;

		virtual void 		run() = 0;
		void				closeWindow() const;
		[[nodiscard]] bool	isKeyDown(keys_t key) const;
		static GLuint		loadTexture(const char* path);
		static void			toggleWireframe(bool showWireframe);
};

#endif
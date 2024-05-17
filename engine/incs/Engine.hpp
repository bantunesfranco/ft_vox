#ifndef ENGINE_HPP
#define ENGINE_HPP

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
 
#include "linmath.h"
 
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <cstdint>
#include <cassert>
#include <exception>

#include "Renderer.hpp"
#include "Camera.hpp"
#include "defines.hpp"

#define VOX_ASSERT(val, str) assert(val && str);
#define VOX_NONNULL(val) assert(val && "Value cannot be null");

#define BLOCK_SIZE 1.0f

class Engine
{
	private:
		Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&);

		static Engine*		_instance;
		static int32_t		settings[VOX_SETTINGS_MAX];

		int32_t				_width;
		int32_t				_height;
		double				_lastFrameTime;

		void				_initWindow(int32_t width, int32_t height, const char* title, bool resize);

	public:
		~Engine() = default;

		class EngineException : public std::exception
		{
			public:
				EngineException(vox_errno_t err);
				const char* what() const noexcept;
		};

		static vox_errno_t	vox_errno;
		GLFWwindow*			window;
		Renderer*			renderer;
		Camera*				camera;
	
		static const char*	vox_strerror(vox_errno_t val);
		static Engine*		initEngine(int32_t width, int32_t height, const char* title, bool resize);
		static Engine*		getInstance() { return _instance; };
		static void 		setSetting(int32_t setting, bool value);

		void				terminateEngine();


		void				setKeyCallback(GLFWkeyfun callback);
		void				setMouseButtonCallback(GLFWmousebuttonfun callback);
		void				setCursorPosCallback(GLFWcursorposfun callback);
		void				setScrollCallback(GLFWscrollfun callback);
		void				setResizeCallback(GLFWwindowsizefun callback);
		void				setFramebufferSizeCallback(GLFWframebuffersizefun callback);
		void				setWindowCloseCallback(GLFWwindowclosefun callback);
		void				setErrorCallback(GLFWerrorfun callback);

		void 				run();
		void				closeWindow();
		double				getDeltaTime();
		void				setFrameTime();
		bool				isKeyDown(key_t key);

};


#endif
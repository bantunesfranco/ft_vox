#ifndef ENGINE_HPP
#define ENGINE_HPP
 
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <cstdint>
#include <cassert>
#include <exception>
#include <map>

#include "Renderer.hpp"
#include "Camera.hpp"
#include "defines.hpp"

#define VOX_ASSERT(val, str) assert(val && str);
#define VOX_NONNULL(val) assert(val && "Value cannot be null");

#define BLOCK_SIZE 1.0f

class Engine
{
	private:
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&);

		int32_t				_width;
		int32_t				_height;
		double				_lastFrameTime;

		void				initWindow(int32_t width, int32_t height, const char* title);

	public:
		Engine(int32_t width, int32_t height, const char* title, std::map<settings_t, bool> settings);
		virtual ~Engine();

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
		int32_t				settings[VOX_SETTINGS_MAX];
	
		static const char*	vox_strerror(vox_errno_t val);
		void				setSetting(int32_t setting, bool value);

		bool				windowIsOpen(GLFWwindow* window);
		void				terminate();

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
		bool				isKeyDown(keys_t key);
		// GLuint				loadTexture(const char* path);

};


#endif
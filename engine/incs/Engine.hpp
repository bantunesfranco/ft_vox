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
#include "defines.hpp"

#define VOX_ASSERT(val, str) assert(val && str);
#define VOX_NONNULL(val) assert(val && "Value cannot be null");

class Engine
{
	private:
		Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&);

		static Engine*		_instance;

		int32_t				_width;
		int32_t				_height;
		int32_t				_settings[VOX_SETTINGS_MAX];

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
	
		static const char*	vox_strerror(vox_errno_t val);
		static Engine*		initEngine(int32_t width, int32_t height, const char* title, bool resize);
		// static Engine*		getInstance() { return _instance; };

	

		void				terminateEngine();
		void 				setSetting(int32_t setting, bool value);
};


#endif
#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <cstdint>
#include <cassert>
#include "GLFW/glfw3.h"

#define ENG_ASSERT(val, str) assert(val && str);
#define ENG_NONNULL(val) assert(val && "Value cannot be null");

class Engine
{
	private:
		Engine();
		~Engine() = default;
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&);

		static Engine* _instance;

		int32_t _width;
		int32_t _height;
		int32_t _settings[20];
		// context



	public:
		static Engine* initEngine(int32_t width, int32_t height, const char* title);
		static Engine* getInstance() { return *_instance; }

		void terminateEngine();
		void setSetting(int32_t setting, bool value);
	

		GLFWwindow* window;
		Renderer* renderer;
};


#endif
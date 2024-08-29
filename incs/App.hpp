#ifndef APP_HPP
#define APP_HPP

#include "Engine.hpp"

class App : public Engine
{
	public:
		App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool> settings);
		~App() = default;
		App(const App&) = delete;
		App& operator=(const App&) = delete;

		void	run();
		void	terminate();
	
	private:
		void	setCallbackFunctions(void);
};

void error_callback(int error, const char* description);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

#endif
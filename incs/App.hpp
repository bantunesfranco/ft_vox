#ifndef APP_HPP
#define APP_HPP

#include "Engine.hpp"
#include "World.hpp"

class App : public Engine
{
	public:
		App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool>& settings);
		~App() override = default;
		App(const App&) = delete;
		App& operator=(const App&) = delete;

		void	run() override;
		void	terminate() override;

		bool	_showWireframe;
		std::unordered_map<BlockType, GLuint> textures;
	
	private:
		void	setCallbackFunctions() const;
		void	loadTextures();
};

void error_callback(int error, const char* description);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

void setupImGui(GLFWwindow* window);
void renderImGui(const std::unique_ptr<Camera>& camera, bool showWireframe);

#endif
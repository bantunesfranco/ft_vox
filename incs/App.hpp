#ifndef APP_HPP
#define APP_HPP

#include <array>
#include <cstdint>

#include "Block.hpp"
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

		bool			_showWireframe{};
		GLuint			textureAtlas{};

	private:
		void	setCallbackFunctions() const;
		void	loadTextures();
		void	renderWorld(const World& world);
		void	processMeshUploads(World& world);

};

void error_callback(int error, const char* description);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

void setupImGui(GLFWwindow* window);
// void renderImGui(const std::unique_ptr<Camera>& camera, bool showWireframe);
void renderImGui(const std::unique_ptr<Camera>& camera, bool showWireframe, float rgba[4]);



#endif

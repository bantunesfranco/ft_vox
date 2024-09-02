#include "App.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <iostream>

App::App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool> settings) : Engine(width, height, title, settings) {

	_showWireframe = false;
	setCallbackFunctions();
	setupImGui(window);
    loadTextures();
}

void App::setCallbackFunctions(void)
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
    World world(textures);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::mat4 mvp;

	while (windowIsOpen(window))
	{
		glfwPollEvents();
		
        world.updateChunks(camera->pos);
        world.generateWorldMesh(vertices, indices);
        renderer->initProjectionMatrix(window, camera, &mvp);
        renderer->render(vertices, indices, &mvp);

		renderImGui(camera, _showWireframe);
		glfwSwapBuffers(window);
		setFrameTime();

        vertices.clear();
        indices.clear();
	}
}

void App::terminate()
{

	if (ImGui::GetCurrentContext())
	{
		ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Engine::terminate();
}

void App::loadTextures(void) {
    try
    {
        // textures[BlockType::Grass] = loadTexture("./textures/grass.png");
        // textures[BlockType::Dirt] = loadTexture("./textures/dirt.png");
        // textures[BlockType::Stone] = loadTexture("./textures/stone.png");
        // textures[BlockType::Sand] = loadTexture("./textures/sand.png");
        // textures[BlockType::Water] = loadTexture("./textures/water.png");
        textures[BlockType::Grass] = loadTexture("./textures/grass.png");
        textures[BlockType::Stone] = loadTexture("./textures/amethyst.png");
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        terminate();
        std::exit(Engine::vox_errno);
    }

}
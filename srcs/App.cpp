#include "App.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

App::App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool> settings) : Engine(width, height, title, settings) {

	_showWireframe = false;
	setCallbackFunctions();
	setupImGui(window);

}

void App::setCallbackFunctions(void)
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
    World world;
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

void App::generateTerrain(Chunk& chunk) {
    const float frequency = 0.05f;
    const float amplitude = 20.0f;

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            // Basic sine wave terrain with added randomness
            float height = amplitude * (std::sin(frequency * x) + std::sin(frequency * z)) +
                           (rand() % 10 - 5) +  // Random value to add noise
                           (Chunk::HEIGHT / 4);

            int intHeight = static_cast<int>(height);

            for (int y = 0; y < intHeight; ++y) {
                uint32_t voxelData = packVoxelData(1, 100, 255, 100, 1);  // Example voxel data (active, color, block type)
                chunk.setVoxel(x, y, z, voxelData);
            }
        }
    }
}
#include "App.hpp"

#include <iostream>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

static void handleMovement(const GLFWwindow* window, std::unique_ptr<Camera>& camera)
{
	glm::vec3 moveDir(0.0f);

	auto app = static_cast<App*>(glfwGetWindowUserPointer(const_cast<GLFWwindow*>(window)));
	float dt = FPSCounter::getDeltaTime();

	// clamp very large dt (pause/hitch) to avoid huge jumps
	if (dt <= 0.0f) dt = 0.0001f;
	dt = glm::min(dt, 0.1f);

	// Movement directions
	auto forward = glm::normalize(camera->dir);
	auto right = glm::normalize(glm::cross(forward, camera->up));

	bool moved = false;
	if (app->isKeyDown(VOX_KEY_W)) { moveDir += forward; moved = true; }
	if (app->isKeyDown(VOX_KEY_S)) { moveDir -= forward; moved = true; }
	if (app->isKeyDown(VOX_KEY_A)) { moveDir -= right;   moved = true; }
	if (app->isKeyDown(VOX_KEY_D)) { moveDir += right;   moved = true; }

	if (moved && glm::length(moveDir) > 0.0f)
	{
		moveDir = glm::normalize(moveDir) * camera->moveSpeed * dt;
		camera->pos += moveDir;
	}

	// Camera rotation
	bool rotated = false;
	if (app->isKeyDown(VOX_KEY_UP)) {
		camera->pitch += camera->rotSpeed * dt;
		camera->pitch = glm::min(camera->pitch, 89.0f);
		rotated = true;
	}
	if (app->isKeyDown(VOX_KEY_DOWN)) {
		camera->pitch -= camera->rotSpeed * dt;
		camera->pitch = glm::max(camera->pitch, -89.0f);
		rotated = true;
	}
	if (app->isKeyDown(VOX_KEY_LEFT)) {
		camera->yaw -= camera->rotSpeed * dt;
		rotated = true;
	}
	if (app->isKeyDown(VOX_KEY_RIGHT)) {
		camera->yaw += camera->rotSpeed * dt;
		rotated = true;
	}

	if (rotated)
	{
		const float pitchRad = glm::radians(camera->pitch);
		const float yawRad = glm::radians(camera->yaw);

		glm::vec3 dir{cosf(yawRad) * cosf(pitchRad) ,sinf(pitchRad) ,sinf(yawRad) * cosf(pitchRad)};
		camera->dir = glm::normalize(dir);
	}
}

App::App(const int32_t width, const int32_t height, const char *title, std::map<settings_t, bool>& settings) : Engine(width, height, title, settings) {

	_showWireframe = false;
	setCallbackFunctions();
	setupImGui(window);
    loadTextures();
}

void App::setCallbackFunctions() const
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
    ThreadPool threadPool(8);
    World world(textures);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    glm::mat4 mvp;

	while (windowIsOpen(window))
	{
		glfwPollEvents();
        FPSCounter::update();
		handleMovement(window, camera);

        Renderer::initProjectionMatrix(window, camera, mvp);
        world.updateChunks(camera->pos, threadPool);
        world.generateWorldMesh(renderer, camera, vertices, indices);
        renderer->render(vertices, indices, mvp);

		renderImGui(camera, _showWireframe);
		glfwSwapBuffers(window);

        vertices.clear();
        indices.clear();

        // renderer->releaseVBO();
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

void App::loadTextures() {
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

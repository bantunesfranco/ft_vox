#include "App.hpp"

#include <iostream>

#include <imgui.h>
#include <ranges>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "ThreadPool.hpp"
#include "glm/gtx/norm.hpp"

static void handleMovement(const GLFWwindow* window, std::unique_ptr<Camera>& camera) {
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

	if (moved && glm::length(moveDir) > 0.0f) {
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

	if (rotated) {
		const float pitchRad = glm::radians(camera->pitch);
		const float yawRad = glm::radians(camera->yaw);

		glm::vec3 dir{cosf(yawRad) * cosf(pitchRad) ,sinf(pitchRad) ,sinf(yawRad) * cosf(pitchRad)};
		camera->dir = glm::normalize(dir);
	}
}

App::App(const int32_t width, const int32_t height, const char *title, std::map<settings_t, bool>& settings)
	: Engine(width, height, title, settings) {

	_showWireframe = false;
	setCallbackFunctions();
	setupImGui(window);
    loadTextures();
}

void App::setCallbackFunctions() const {
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run() {
    ThreadPool threadPool(
        std::max(
            1u,
            std::thread::hardware_concurrency() > 2
                ? std::thread::hardware_concurrency() - 2
                : 1u
        )
    );

    World world;

    float rgba[4] = {
        0.075f,
        0.33f,
        0.61f,
        1.0f
    };

    while (windowIsOpen(window))
    {
        glClearColor(
            0.075f,
            0.33f,
            0.61f,
            1.0f
        );

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwPollEvents();

        FPSCounter::update();

        handleMovement(window, camera);

        world.frustum.updateFrustum(camera->proj, camera->view);
        world.updateChunks(camera->pos, threadPool);
		for (auto& gpuMesh : world.pruneRenderStates())
			renderer->releaseMesh(gpuMesh);
    	world.buildRenderStates();

        WorldUBO worldUBO{};
    	Renderer::initProjectionMatrix(window, camera, worldUBO.MVP);

        renderer->beginFrame(worldUBO);

    	processMeshUploads(world);

        renderWorld(world);

        renderImGui(camera, _showWireframe, rgba);

        glfwSwapBuffers(window);
    }
}


void App::terminate() {

	if (ImGui::GetCurrentContext()) {
		ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Engine::terminate();
}

void App::loadTextures() {
	try {
		const std::vector<std::string> paths = {
			"./textures/grass.png",
			"./textures/dirt.png",
			"./textures/stone.png",
			"./textures/sand.png",
			"./textures/amethyst.png"
		};

		int texWidth = 0;
		int texHeight = 0;

		textureArray = loadTextureArray(
			paths,
			texWidth,
			texHeight
		);

		renderer->setTexArray(textureArray);
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';

		terminate();

		std::exit(vox_errno);
	}
}

void App::renderWorld(const World& world) {
	for (const auto& renderState : world.getRenderStates()) {
		if (!renderState.gpuMesh.uploaded)
			continue;

		if (renderState.gpuMesh.indexCount == 0)
			continue;

		if (!world.frustum.isBoxInFrustum(renderState.worldMin, renderState.worldMax))
			continue;

		renderer->renderMesh(renderState.gpuMesh, renderState.worldPosition);
	}
}

void App::processMeshUploads(World& world) {
	for (auto& renderState : world.getRenderStates()) {
		if (renderState.mesh.empty())
			continue;

		if (renderState.gpuMesh.uploaded)
			continue;

		renderer->uploadMesh(renderState.mesh, renderState.gpuMesh);

		renderState.mesh.clear();
	}
}

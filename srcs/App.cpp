#include "App.hpp"

#include <iostream>

#include <imgui.h>
#include <ranges>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

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

App::App(const int32_t width, const int32_t height, const char *title, std::map<settings_t, bool>& settings)
	: Engine(width, height, title, settings) {

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
    World world(textureIndices);

	float rgba[4] = {0.075f, 0.33f, 0.61f, 1.f};

	while (windowIsOpen(window))
	{
		glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
		glfwPollEvents();
        FPSCounter::update();
		handleMovement(window, camera);

        Renderer::initProjectionMatrix(window, camera, world.worldUBO.MVP);
		world.frustum.updateFrustum(camera->proj, camera->view);

		world.updateChunks(camera->pos, threadPool);

		std::vector<Chunk*> visibleChunks;
		{
			std::lock_guard lock(world.chunk_mutex);
			auto chunks = world.getChunks() | std::views::values;
			visibleChunks.reserve(chunks.size());
			for (auto& chunk : chunks) {
				if (chunk.cachedVertices.empty())
					continue;
				if (!world.frustum.isBoxInFrustum(chunk.worldMin, chunk.worldMax))
					continue;
				visibleChunks.push_back(&chunk);
			}
		}

		glm::vec3 camPos = camera->pos;
		std::sort(visibleChunks.begin(), visibleChunks.end(),
			[&camPos](const Chunk* a, const Chunk* b) {
				return glm::distance2(a->worldCenter, camPos) < glm::distance2(b->worldCenter, camPos);
			});

		for (const auto& chunk : visibleChunks) {
			uploadChunk(*chunk, chunk->renderData);
			renderChunk(*chunk, world.worldUBO, world.ubo);
		}

		renderImGui(camera, _showWireframe, rgba);
		glfwSwapBuffers(window);
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
    	const std::vector<std::string> paths = {
    		"./textures/grass.png",      // texIndex 0
    		"./textures/dirt.png",		// texIndex 1
    		"./textures/stone.png",
    		// "./textures/sand.png",
			// "./textures/amethyst.png",
		};

    	int texWidth, texHeight;
    	textureArray = loadTextureArray(paths, texWidth, texHeight);

    	// textureIndices[BlockType::Air] = -1;
        textureIndices[BlockType::Grass] = 0;
        textureIndices[BlockType::Dirt] = 1;
        textureIndices[BlockType::Stone] = 2;

    	renderer->setTexArray(textureArray);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        terminate();
        std::exit(Engine::vox_errno);
    }
}

void App::uploadChunk(const Chunk& chunk, Chunk::ChunkRenderData& data)
{
	if (chunk.cachedVertices.empty() || chunk.cachedIndices.empty())
		return;

	if (data.vao == 0)
		glGenVertexArrays(1, &data.vao);
	if (data.vbo == 0)
		data.vbo = VBOManager::get().getVBO();
	if (data.ibo == 0)
		data.ibo = VBOManager::get().getVBO();

	data.indexCount = chunk.cachedIndices.size();

	glBindVertexArray(data.vao);

	glBindBuffer(GL_ARRAY_BUFFER, data.vbo);
	glBufferData(GL_ARRAY_BUFFER, chunk.cachedVertices.size() * sizeof(Vertex),
				 chunk.cachedVertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indexCount * sizeof(uint32_t),
				 chunk.cachedIndices.data(), GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3,
		GL_FLOAT, GL_FALSE,
		sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3,
		GL_FLOAT, GL_FALSE,
		sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2,
		GL_FLOAT, GL_FALSE,
		sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));

	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 1,
		GL_UNSIGNED_INT,
		sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texIndex)));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 1,
		GL_FLOAT, GL_FALSE,
		sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, ao)));

	glBindVertexArray(0);
}

void App::renderChunk(const Chunk& chunk, const WorldUBO& worldUbo, const GLuint ubo) const
{
	glUseProgram(renderer->getShaderProgram());
	glBindVertexArray(chunk.renderData.vao);

	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WorldUBO), &worldUbo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glBindTextureUnit(0, renderer->getTextureArray());
	glDrawElements(GL_TRIANGLES, chunk.renderData.indexCount, GL_UNSIGNED_INT, nullptr);
}

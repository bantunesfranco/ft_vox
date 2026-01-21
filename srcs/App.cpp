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
	setupImGui(window);
	setCallbackFunctions();
    loadTextures();
	threadPool = std::make_unique<ThreadPool>(8);
}

App::~App() { App::terminate(); }

void App::setCallbackFunctions() const
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
}

void App::run()
{
    World world(textureIndices);

    float rgba[4] = {0.075f, 0.33f, 0.61f, 1.f};

    while (windowIsOpen(window))
    {
        glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        glfwPollEvents();
        FPSCounter::update();
        handleMovement(window, camera);

        Renderer::initProjectionMatrix(window, camera, world.worldUBO.MVP);
        world.updateFrustum(camera->proj, camera->view);

        world.updateChunks(camera->pos, *threadPool);

        std::vector<Chunk*> visibleChunks;
        // std::vector<std::pair<glm::ivec2, Chunk*>> chunksToCalcAO;

        {
            std::lock_guard lock(world.chunk_mutex);
            auto& chunks = world.getChunks();
            visibleChunks.reserve(chunks.size());

            for (auto& [coord, chunk] : chunks) {
                if (chunk.cachedVertices.empty())
                    continue;
                if (!world.isBoxInFrustum(chunk.worldMin, chunk.worldMax))
                    continue;

                // Queue AO calculation if not already done
                // if (!chunk.aoCalculated) {
                //     chunksToCalcAO.emplace_back(coord, &chunk);
                //     chunk.aoCalculated = true;
                // }

            	// for (auto& [coord, chunk] : chunks) {
            		if (!chunk.aoCalculated) {
            			calcChunkAO(coord, chunk, world);  // Direct call, no threading
            			chunk.aoCalculated = true;
            		}
            	// }

                visibleChunks.push_back(&chunk);
            }
        }

        glm::vec3 camPos = camera->pos;
        std::sort(visibleChunks.begin(), visibleChunks.end(),
            [&camPos](const Chunk* a, const Chunk* b) {
                const auto worldCenterA = (a->worldMin + a->worldMax) * 0.5f;
                const auto worldCenterB = (b->worldMin + b->worldMax) * 0.5f;
                return glm::distance2(worldCenterA, camPos) < glm::distance2(worldCenterB, camPos);
            });

        for (const auto& chunk : visibleChunks) {
            uploadChunk(*chunk, chunk->renderData);
            renderChunk(*chunk, world.worldUBO, world.ubo);
        }

        renderImGui(camera, _showWireframe, rgba, world.getChunks().size());
        glfwSwapBuffers(window);

		// chunksToCalcAO.clear();
	}

	threadPool->wait();
}

void App::terminate()
{
	threadPool.reset();
	if (ImGui::GetCurrentContext())
	{
		ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void App::loadTextures() {
    try
    {
    	const std::vector<std::string> paths = {
    		"./textures/grass.png",      // texIndex 0
    		"./textures/dirt.png",		// texIndex 1
    		"./textures/stone.png",
    		"./textures/sand.png",
    		"./textures/iron_ore.png",
    		"./textures/snow.png",
			// "./textures/amethyst.png",
		};

    	int texWidth, texHeight;
    	textureArray = loadTextureArray(paths, texWidth, texHeight);

    	// textureIndices[BlockType::Air] = -1;
        textureIndices[static_cast<int>(BlockType::Grass)] = 0;
        textureIndices[static_cast<int>(BlockType::Dirt)] = 1;
        textureIndices[static_cast<int>(BlockType::Stone)] = 2;
    	textureIndices[static_cast<int>(BlockType::Sand)] = 3;
    	textureIndices[static_cast<int>(BlockType::IronOre)] = 4;
    	textureIndices[static_cast<int>(BlockType::Snow)] = 5;

    	renderer->setTexArray(textureArray);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        terminate();
        std::exit(vox_errno);
    }
}

void App::uploadChunk(const Chunk& chunk, Chunk::ChunkRenderData& data)
{
	if (chunk.cachedIndices.empty()) return;

	if (data.vao == 0) glGenVertexArrays(1, &data.vao);
	if (data.vbo == 0) data.vbo = VBOManager::get().getVBO();
	if (data.ibo == 0) data.ibo = VBOManager::get().getVBO();

	data.indexCount = chunk.cachedIndices.size();

	glBindVertexArray(data.vao);

	glBindBuffer(GL_ARRAY_BUFFER, data.vbo);
	glBufferData(GL_ARRAY_BUFFER, chunk.cachedVertices.size() * sizeof(Vertex),
				 chunk.cachedVertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indexCount * sizeof(uint32_t),
				 chunk.cachedIndices.data(), GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, position)));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, uv)));

	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_SHORT, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, texIndex)));

	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, normal)));

	glEnableVertexAttribArray(4);
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, sizeof(Vertex),
		(void*)offsetof(Vertex, ao));

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

void App::calcChunkAO(const glm::ivec2& coord, Chunk& chunk, const World& world)
{
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    const auto& chunks = world.getChunks();

    // Pre-fetch neighbor chunks
    const Chunk* neighbors[9] = {nullptr};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = chunks.find(coord + glm::ivec2(dx, dz));
            neighbors[(dz + 1) * 3 + (dx + 1)] = (it != chunks.end()) ? &it->second : nullptr;
        }
    }

    auto getBlock = [&](int x, int y, int z) -> bool {
        if (y < 0 || y >= H) return false;

        int cx = x, cz = z;
        int nidx_x = 1, nidx_z = 1;

        if (x < 0) { nidx_x = 0; cx += W; }
        else if (x >= W) { nidx_x = 2; cx -= W; }

        if (z < 0) { nidx_z = 0; cz += D; }
        else if (z >= D) { nidx_z = 2; cz -= D; }

        const Chunk* neighbor = neighbors[nidx_z * 3 + nidx_x];
        return neighbor ? neighbor->isBlockActive(cx, y, cz) : false;
    };

	constexpr auto calcAO = [](bool s1, bool s2, bool c) -> uint8_t {
		int darkness = (s1 ? 1 : 0) + (s2 ? 1 : 0) + (c ? 1 : 0);
		return std::max(1, 3 - darkness);  // Never go below 1 (never fully dark)
	};

    const size_t vertexCount = chunk.cachedVertices.size();
    const glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);

    for (size_t i = 0; i < vertexCount; ++i) {
        auto& v = chunk.cachedVertices[i];

        // Remove offset to get local chunk coordinates
        glm::vec3 localPos = v.position - offset;
        int vx = static_cast<int>(localPos.x);
        int vy = static_cast<int>(localPos.y);
        int vz = static_cast<int>(localPos.z);

        uint8_t normalIdx = v.normal;

        // Use lookup table for corner detection
        constexpr uint8_t cornerLUT[4] = {0, 1, 3, 2};
        uint8_t corner = cornerLUT[i % 4];

        bool cornerX = corner & 1;
        bool cornerY = corner & 2;

        uint8_t ao = 3;

        if (normalIdx < 2) { // X faces
            int axq = vx + (normalIdx == 0 ? 1 : 0);
            int py = cornerY ? vy + 1 : vy - 1;
            int pz = cornerX ? vz + 1 : vz - 1;

            ao = calcAO(
                getBlock(axq, py, vz),
                getBlock(axq, vy, pz),
                getBlock(axq, py, pz)
            );
        }
        else if (normalIdx < 4) { // Y faces
            int ayq = vy + (normalIdx == 2 ? 1 : 0);
            int px = cornerX ? vx + 1 : vx - 1;
            int pz = cornerX ? vz + 1 : vz - 1;

            ao = calcAO(
                getBlock(px, ayq, vz),
                getBlock(vx, ayq, pz),
                getBlock(px, ayq, pz)
            );
        }
        else { // Z faces
            int azq = vz + (normalIdx == 4 ? 1 : 0);
            int px = cornerX ? vx + 1 : vx - 1;
            int py = cornerY ? vy + 1 : vy - 1;

            ao = calcAO(
                getBlock(px, vy, azq),
                getBlock(vx, py, azq),
                getBlock(px, py, azq)
            );
        }

        v.ao = ao;
    }
}

void App::queueVisibleChunksAO(World& world, const std::vector<std::pair<glm::ivec2, Chunk*>>& chunksToCalcAO, ThreadPool& threadPool)
{
	for (const auto& coord : chunksToCalcAO | std::views::keys) {
		threadPool.enqueue([coord, &world]() {
			std::lock_guard lock(world.chunk_mutex);
			if (const auto it = world.getChunks().find(coord); it != world.getChunks().end())
				calcChunkAO(coord, it->second, world);
		});
	}
}
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
	: Engine(width, height, title, settings), textureIndices(), world(textureIndices)
{
	showWireframe = false;
	focused = true;
	setCallbackFunctions();
	loadTextures();
	threadPool = std::make_unique<ThreadPool>(8);
	setupHighlightCube();
	setupImGui(window);
}

App::~App()
{
	cleanupHighlightCube();
	App::terminate();
}

void App::setCallbackFunctions() const
{
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setMouseButtonCallback(mouse_bttn_callback);
	setCursorPosCallback(cursor_callback);
}

void App::run()
{
    // float rgba[4] = {0.075f, 0.33f, 0.61f, 1.f};
    float rgba[4] = {};

    while (windowIsOpen(window))
    {
        glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        glfwPollEvents();
        FPSCounter::update();
        handleMovement(window, camera);

        Renderer::initProjectionMatrix(window, camera, world.worldUBO.MVP);
        world.updateFrustum(camera->proj, camera->view);

        world.updateChunks(camera->pos, *threadPool);

    	updateBlockHighlight();

        {
	        std::vector<Chunk*> visibleChunks;
	        std::lock_guard lock(world.chunk_mutex);
        	auto& chunks = world.getChunks();
        	visibleChunks.reserve(chunks.size());

        	for (auto& [coord, chunk] : chunks) {
        		if (!world.isBoxInFrustum(chunk.worldMin, chunk.worldMax))
        			continue;

        		if (!chunk.cachedOpaqueVertices.empty() || !chunk.cachedTransparentVertices.empty())
        			uploadChunk(chunk, chunk.renderData);

        		if (!chunk.aoCalculated) {
        			calcChunkAO(coord, chunk, world);
        			chunk.aoCalculated = true;
        		}

        		if (chunk.renderData.opaque.vao || chunk.renderData.transparent.vao)
        			visibleChunks.push_back(&chunk);
        	}

        	glm::vec3 camPos = camera->pos;
        	std::sort(visibleChunks.begin(), visibleChunks.end(),
				[&camPos](const Chunk* a, const Chunk* b) {
					const auto worldCenterA = (a->worldMin + a->worldMax) * 0.5f;
					const auto worldCenterB = (b->worldMin + b->worldMax) * 0.5f;
					return glm::distance2(worldCenterA, camPos) < glm::distance2(worldCenterB, camPos);
				});

        	for (const auto& chunk : visibleChunks) {
        		renderChunk(*chunk, world.worldUBO, world.ubo, RenderType::Opaque);
        	}

        	for (const auto& chunk : visibleChunks) {
        		renderChunk(*chunk, world.worldUBO, world.ubo, RenderType::Transparent);
        	}
	    }

    	renderBlockHighlight();
        renderImGui(camera, showWireframe, rgba, world.getChunks().size());
        glfwSwapBuffers(window);

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
			"./textures/water.png",
    		"./textures/iron_ore.png",
    		"./textures/snow.png",
			"./textures/amethyst.png",
		};

    	int texWidth, texHeight;
    	textureArray = loadTextureArray(paths, texWidth, texHeight);

    	// textureIndices[BlockType::Air] = -1;
        textureIndices[static_cast<int>(BlockType::Grass)] = 0;
        textureIndices[static_cast<int>(BlockType::Dirt)] = 1;
        textureIndices[static_cast<int>(BlockType::Stone)] = 2;
    	textureIndices[static_cast<int>(BlockType::Sand)] = 3;
    	textureIndices[static_cast<int>(BlockType::Water)] = 4;
    	textureIndices[static_cast<int>(BlockType::IronOre)] = 5;
    	textureIndices[static_cast<int>(BlockType::Snow)] = 6;
    	textureIndices[static_cast<int>(BlockType::Amethyst)] = 7;

    	renderer->setTexArray(textureArray);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        terminate();
        std::exit(vox_errno);
    }
}

static void uploadBatch(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, Chunk::RenderBatch& batch)
{
	if (indices.empty()) return;

	if (batch.vao == 0) glGenVertexArrays(1, &batch.vao);
	if (batch.vbo == 0) batch.vbo = VBOManager::get().getVBO();
	if (batch.ibo == 0) batch.ibo = VBOManager::get().getVBO();

	batch.indexCount = indices.size();

	glBindVertexArray(batch.vao);

	glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		vertices.size() * sizeof(Vertex),
		vertices.data(),
		GL_DYNAMIC_DRAW
	);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.ibo);
	glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		indices.size() * sizeof(uint32_t),
		indices.data(),
		GL_DYNAMIC_DRAW
	);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void*)offsetof(Vertex, position));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void*)offsetof(Vertex, uv));

	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_SHORT, sizeof(Vertex),
		(void*)offsetof(Vertex, texIndex));

	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex),
		(void*)offsetof(Vertex, normal));

	glEnableVertexAttribArray(4);
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, sizeof(Vertex),
		(void*)offsetof(Vertex, ao));

	glBindVertexArray(0);
}

void App::uploadChunk(const Chunk& chunk, Chunk::ChunkRenderData& data)
{
	uploadBatch(chunk.cachedOpaqueVertices, chunk.cachedOpaqueIndices, data.opaque);
	uploadBatch(chunk.cachedTransparentVertices, chunk.cachedTransparentIndices, data.transparent);
}

void App::renderChunk(const Chunk& chunk, const WorldUBO& worldUbo, const GLuint ubo, const RenderType type) const
{
	glUseProgram(renderer->getShaderProgram());

	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WorldUBO), &worldUbo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glBindTextureUnit(0, renderer->getTextureArray());

	// ==========================
	// OPAQUE PASS
	// ==========================
	if (type == RenderType::Opaque && chunk.renderData.opaque.indexCount)
	{
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		glBindVertexArray(chunk.renderData.opaque.vao);
		glDrawElements(
			GL_TRIANGLES,
			chunk.renderData.opaque.indexCount,
			GL_UNSIGNED_INT,
			nullptr
		);
	}

	// ==========================
	// TRANSPARENT PASS (WATER)
	// ==========================
	if (type == RenderType::Transparent && chunk.renderData.transparent.indexCount)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);

		glBindVertexArray(chunk.renderData.transparent.vao);
		glDrawElements(
			GL_TRIANGLES,
			chunk.renderData.transparent.indexCount,
			GL_UNSIGNED_INT,
			nullptr
		);

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	glBindVertexArray(0);
}

void App::destroyBlock()
{
	if (!camera) return;

	// Get camera position and direction
	const glm::vec3 rayOrigin = camera->pos;
	const glm::vec3 rayDirection = camera->dir;

	// Attempt to destroy block
	blockSystem.destroyBlock(rayOrigin, rayDirection, world);
}

void App::placeBlock()
{
	if (!camera) return;

	// Get camera position and direction
	const glm::vec3 rayOrigin = camera->pos;
	const glm::vec3 rayDirection = camera->dir;

	// Attempt to place stone block (change voxel value as needed)
	const Voxel amethyst = packVoxelData(true, 255, 255, 255, static_cast<uint8_t>(BlockType::Amethyst));
	blockSystem.placeBlock(rayOrigin, rayDirection, world, amethyst);
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

    const glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    constexpr uint8_t cornerLUT[4] = {0, 1, 3, 2};

    // =====================================================================
    // Helper lambda to calculate AO for a single vertex
    // =====================================================================
    auto calcVertexAO = [&](const Vertex& v, size_t vertexIndex) -> uint8_t {
        glm::vec3 localPos = v.position - offset;
        int vx = static_cast<int>(localPos.x);
        int vy = static_cast<int>(localPos.y);
        int vz = static_cast<int>(localPos.z);

        uint8_t normalIdx = v.normal;
        uint8_t corner = cornerLUT[vertexIndex % 4];

        bool cornerX = corner & 1;
        bool cornerY = corner & 2;

        uint8_t ao = 3;

        if (normalIdx < 2) { // X faces (perpendicular to X axis)
            int axq = vx + (normalIdx == 0 ? 1 : 0);
            int py = cornerY ? vy + 1 : vy - 1;
            int pz = cornerX ? vz + 1 : vz - 1;

            ao = calcAO(
                getBlock(axq, py, vz),
                getBlock(axq, vy, pz),
                getBlock(axq, py, pz)
            );
        }
        else if (normalIdx < 4) { // Y faces (perpendicular to Y axis)
            int ayq = vy + (normalIdx == 2 ? 1 : 0);
            int px = cornerX ? vx + 1 : vx - 1;
            int pz = cornerY ? vz + 1 : vz - 1;  // FIXED: was cornerX, should be cornerY

            ao = calcAO(
                getBlock(px, ayq, vz),
                getBlock(vx, ayq, pz),
                getBlock(px, ayq, pz)
            );
        }
        else { // Z faces (perpendicular to Z axis)
            int azq = vz + (normalIdx == 4 ? 1 : 0);
            int px = cornerX ? vx + 1 : vx - 1;
            int py = cornerY ? vy + 1 : vy - 1;

            ao = calcAO(
                getBlock(px, vy, azq),
                getBlock(vx, py, azq),
                getBlock(px, py, azq)
            );
        }

        return ao;
    };

    // =====================================================================
    // Calculate AO for opaque blocks
    // =====================================================================
    for (size_t i = 0; i < chunk.cachedOpaqueVertices.size(); ++i) {
        auto& v = chunk.cachedOpaqueVertices[i];
        v.ao = calcVertexAO(v, i);
    }

    // =====================================================================
    // Calculate AO for transparent blocks (WATER)
    // =====================================================================
    for (size_t i = 0; i < chunk.cachedTransparentVertices.size(); ++i) {
        auto& v = chunk.cachedTransparentVertices[i];
        v.ao = calcVertexAO(v, i);
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

void App::setupHighlightCube()
{
	// 1x1x1 cube
	static constexpr float vertices[] = {
		// bottom
		0,0,0,  1,0,0,  1,0,1,  0,0,1,
		// top
		0,1,0,  1,1,0,  1,1,1,  0,1,1
	};

	static constexpr uint32_t indices[] = {
		// bottom
		0,1, 1,2, 2,3, 3,0,
		// top
		4,5, 5,6, 6,7, 7,4,
		// verticals
		0,4, 1,5, 2,6, 3,7
	};

	highlightedBlock = { glm::vec3(0.f), 0, 0, 0, false };

	glGenVertexArrays(1, &highlightedBlock.highlightVAO);
	glGenBuffers(1, &highlightedBlock.highlightVBO);
	glGenBuffers(1, &highlightedBlock.highlightIBO);

	glBindVertexArray(highlightedBlock.highlightVAO);

	// Position VBO
	glBindBuffer(GL_ARRAY_BUFFER, highlightedBlock.highlightVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	// Index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlightedBlock.highlightIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// ===== CONSTANT ATTRIBUTES (IMPORTANT PART) =====

	// UV (location = 1)
	glDisableVertexAttribArray(1);
	glVertexAttrib2f(1, 0.0f, 0.0f);

	// Texture index (location = 2)
	glDisableVertexAttribArray(2);
	glVertexAttribI1ui(2, 0); // MUST be a valid opaque texture layer

	// Normal (location = 3) → +Y
	glDisableVertexAttribArray(3);
	glVertexAttribI1ui(3, 2); // normals[2] = (0,1,0)

	// AO (location = 4) → max
	glDisableVertexAttribArray(4);
	glVertexAttribI1ui(4, 0);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}


void App::cleanupHighlightCube()
{
    if (highlightedBlock.highlightVAO) {
        glDeleteVertexArrays(1, &highlightedBlock.highlightVAO);
        glDeleteBuffers(1, &highlightedBlock.highlightVBO);
        glDeleteBuffers(1, &highlightedBlock.highlightIBO);
        highlightedBlock.highlightVAO = 0;
    }
}

void App::updateBlockHighlight()
{
    if (!camera) return;

	const RaycastHit hit = blockSystem.raycastBlocks(camera->pos, camera->dir, world);
    if (hit.isValid) {
        highlightedBlock.highlightedBlockPos = hit.blockPos;
        highlightedBlock.isHighlighted = true;
    } else {
        highlightedBlock.isHighlighted = false;
    }
}

void App::renderBlockHighlight()
{
	if (!highlightedBlock.isHighlighted || highlightedBlock.highlightVAO == 0)
		return;

	// Model matrix (inflate slightly to avoid z-fighting)
	glm::mat4 model = glm::translate(
		glm::mat4(1.0f),
		highlightedBlock.highlightedBlockPos
	);
	model = glm::scale(model, glm::vec3(1.005f));

	world.worldUBO.MVP = camera->proj * camera->view * model;

	glUseProgram(renderer->getShaderProgram());

	// Update UBO
	glBindBuffer(GL_UNIFORM_BUFFER, world.ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WorldUBO), &world.worldUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glBindTextureUnit(0, renderer->getTextureArray());

	glBindVertexArray(highlightedBlock.highlightVAO);

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	glLineWidth(5.0f);
	glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
	glLineWidth(1.0f);

	glDepthMask(GL_TRUE);
	glBindVertexArray(0);
}

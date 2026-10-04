#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <imgui.h>
#include <ranges>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "ThreadPool.hpp"
#include "glm/gtx/norm.hpp"
#include "PlayerCollision.hpp"

namespace {
constexpr float GRAVITY = 24.0f;
constexpr float JUMP_SPEED = 8.5f;

bool collidesWithWorld(const glm::vec3& eyePosition, const World& world) {
	const int minX = static_cast<int>(std::floor(eyePosition.x - PlayerCollision::HALF_WIDTH + PlayerCollision::EPSILON));
	const int maxX = static_cast<int>(std::floor(eyePosition.x + PlayerCollision::HALF_WIDTH - PlayerCollision::EPSILON));
	const int minY = static_cast<int>(std::floor(eyePosition.y - PlayerCollision::EYE_HEIGHT + PlayerCollision::EPSILON));
	const int maxY = static_cast<int>(std::floor(eyePosition.y + PlayerCollision::HEIGHT - PlayerCollision::EYE_HEIGHT - PlayerCollision::EPSILON));
	const int minZ = static_cast<int>(std::floor(eyePosition.z - PlayerCollision::HALF_WIDTH + PlayerCollision::EPSILON));
	const int maxZ = static_cast<int>(std::floor(eyePosition.z + PlayerCollision::HALF_WIDTH - PlayerCollision::EPSILON));

	for (int y = minY; y <= maxY; ++y) {
		if (y < 0)
			return true;
		if (y >= ChunkColumn::HEIGHT)
			continue;
		for (int z = minZ; z <= maxZ; ++z) {
			for (int x = minX; x <= maxX; ++x) {
				BlockId block = AIR_BLOCK;
				if (!world.getBlockAt({x, y, z}, block) || world.getBlockRegistry().isOpaque(block))
					return true;
			}
		}
	}

	return false;
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
	setMouseButtonCallback(mouse_button_callback);
}

void App::updatePlayerMovement() {
	if (!camera)
		return;

	float dt = FPSCounter::getDeltaTime();
	if (dt <= 0.0f)
		dt = 0.0001f;
	dt = glm::min(dt, 0.1f);

	bool rotated = false;
	if (isKeyDown(VOX_KEY_UP)) {
		camera->pitch = glm::min(camera->pitch + camera->rotSpeed * dt, 89.0f);
		rotated = true;
	}
	if (isKeyDown(VOX_KEY_DOWN)) {
		camera->pitch = glm::max(camera->pitch - camera->rotSpeed * dt, -89.0f);
		rotated = true;
	}
	if (isKeyDown(VOX_KEY_LEFT)) {
		camera->yaw -= camera->rotSpeed * dt;
		rotated = true;
	}
	if (isKeyDown(VOX_KEY_RIGHT)) {
		camera->yaw += camera->rotSpeed * dt;
		rotated = true;
	}
	if (rotated) {
		const float pitch = glm::radians(camera->pitch);
		const float yaw = glm::radians(camera->yaw);
		camera->dir = glm::normalize(glm::vec3{
			std::cos(yaw) * std::cos(pitch),
			std::sin(pitch),
			std::sin(yaw) * std::cos(pitch)
		});
	}

	if (!spawnInitialized)
		return;

	glm::vec3 forward = camera->dir;
	glm::vec3 right;
	if (!flyMode) {
		forward.y = 0.0f;
		if (glm::dot(forward, forward) < 1e-6f) {
			const float yaw = glm::radians(camera->yaw);
			forward = {std::cos(yaw), 0.0f, std::sin(yaw)};
		}
		forward = glm::normalize(forward);
		right = glm::normalize(glm::cross(forward, camera->up));
	} else {
		forward = glm::normalize(forward);
		right = glm::normalize(glm::cross(forward, camera->up));
	}

	glm::vec3 moveDirection(0.0f);
	if (isKeyDown(VOX_KEY_W)) moveDirection += forward;
	if (isKeyDown(VOX_KEY_S)) moveDirection -= forward;
	if (isKeyDown(VOX_KEY_A)) moveDirection -= right;
	if (isKeyDown(VOX_KEY_D)) moveDirection += right;
	if (flyMode) {
		if (isKeyDown(VOX_KEY_SPACE)) moveDirection.y += 1.0f;
		if (isKeyDown(VOX_KEY_LEFT_CONTROL)) moveDirection.y -= 1.0f;
		if (glm::dot(moveDirection, moveDirection) > 0.0f)
			camera->pos += glm::normalize(moveDirection) * camera->moveSpeed * dt;
		verticalVelocity = 0.0f;
		jumpRequested = false;
		return;
	}

	moveDirection.y = 0.0f;
	if (glm::dot(moveDirection, moveDirection) > 0.0f) {
		const glm::vec3 displacement = glm::normalize(moveDirection) * camera->moveSpeed * dt;
		const auto moveAxis = [this](const int axis, const float amount) {
			const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(amount) / 0.25f)));
			const float step = amount / static_cast<float>(steps);
			for (int i = 0; i < steps; ++i) {
				glm::vec3 candidate = camera->pos;
				candidate[axis] += step;
				if (collidesWithWorld(candidate, world))
					break;
				camera->pos = candidate;
			}
		};
		moveAxis(0, displacement.x);
		moveAxis(2, displacement.z);
	}

	grounded = collidesWithWorld(camera->pos - glm::vec3(0.0f, 0.04f, 0.0f), world);
	if (jumpRequested && grounded) {
		verticalVelocity = JUMP_SPEED;
		grounded = false;
	}
	jumpRequested = false;

	if (grounded && verticalVelocity < 0.0f)
		verticalVelocity = 0.0f;
	else
		verticalVelocity -= GRAVITY * dt;

	const float verticalStep = verticalVelocity * dt;
	const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(verticalStep) / 0.25f)));
	const float step = verticalStep / static_cast<float>(steps);
	for (int i = 0; i < steps; ++i) {
		glm::vec3 candidate = camera->pos;
		candidate.y += step;
		if (collidesWithWorld(candidate, world)) {
			if (step < 0.0f)
				grounded = true;
			verticalVelocity = 0.0f;
			break;
		}
		camera->pos = candidate;
		grounded = false;
	}
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

        world.updateChunks(camera->pos, threadPool);
		if (!spawnInitialized) {
			int surfaceHeight = 0;
			if (world.getSurfaceHeight(
				static_cast<int>(std::floor(camera->pos.x)),
				static_cast<int>(std::floor(camera->pos.z)),
				surfaceHeight
			)) {
				camera->pos.y = static_cast<float>(surfaceHeight + 1) + PlayerCollision::EYE_HEIGHT;
				spawnInitialized = true;
				grounded = true;
				jumpRequested = false;
			}
		}
		updatePlayerMovement();

		world.frustum.updateFrustum(camera->proj, camera->view);
		for (auto& gpuMesh : world.pruneRenderStates())
			renderer->releaseMesh(gpuMesh);
    	world.buildRenderStates();
		updateBlockHighlight();

        WorldUBO worldUBO{};
    	Renderer::initProjectionMatrix(window, camera, worldUBO.MVP);

        renderer->beginFrame(worldUBO);

    	processMeshUploads(world);

        renderWorld(world);
		renderBlockHighlight();

        renderImGui(camera, _showWireframe, rgba);

        glfwSwapBuffers(window);
    }
}

void App::destroyBlock() {
	if (camera)
		blockSystem.removeBlock(camera->pos, camera->dir, world);
}

void App::placeBlock() {
	if (camera)
		blockSystem.placeBlock(
			camera->pos,
			camera->dir,
			toBlockId(BlockType::Amethyst),
			world
		);
}

void App::toggleSpeedBoost() {
	if (!camera)
		return;

	speedBoostEnabled = !speedBoostEnabled;
	camera->moveSpeed = camera->baseMoveSpeed * (speedBoostEnabled ? 10.0f : 1.0f);
	camera->rotSpeed = camera->baseRotSpeed * (speedBoostEnabled ? 2.0f : 1.0f);
}

void App::toggleFlyMode() {
	flyMode = !flyMode;
	verticalVelocity = 0.0f;
	jumpRequested = false;
}

void App::jump() {
	if (!flyMode)
		jumpRequested = true;
}

void App::updateBlockHighlight() {
	if (!camera) {
		hasHighlightedBlock = false;
		return;
	}

	const BlockRaycastHit hit = blockSystem.raycast(camera->pos, camera->dir, world);
	hasHighlightedBlock = hit.valid;
	if (hasHighlightedBlock)
		highlightedBlockPos = hit.blockPos;
}

void App::renderBlockHighlight() {
	if (!hasHighlightedBlock)
		return;

	const glm::vec3 minPos(highlightedBlockPos);
	renderer->renderBoundingBox(minPos, minPos + glm::vec3(1.0f));
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
		textureAtlas = loadTexture("./textures/minecraft_atlas.png");
		glTextureParameteri(textureAtlas, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(textureAtlas, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(textureAtlas, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(textureAtlas, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		constexpr float atlasSize = 2048.0f;
		const auto spriteRect = [atlasSize](const int x, const int y) {
			const float glY = static_cast<float>(y) + 16.0f;

			return glm::vec4(
				(static_cast<float>(x) + 15.0f) / atlasSize,
				(glY + 0.5f) / atlasSize,
				16.0f / atlasSize,
				16.0f / atlasSize
			);
		};

		std::array<glm::vec4, 64> atlasRects{};
		atlasRects.fill(spriteRect(0, 0));
		atlasRects[0] = spriteRect(1408, 528);  // grass_block_top
		atlasRects[1] = spriteRect(1168, 720);  // dirt
		atlasRects[2] = spriteRect(1408, 384);  // grass_block_side
		atlasRects[3] = spriteRect(1680, 1312); // stone
		atlasRects[4] = spriteRect(1440, 1216); // sand
		atlasRects[5] = spriteRect(512, 288);   // amethyst_block
		atlasRects[6] = spriteRect(480, 1504);  // water_still
		atlasRects[7] = spriteRect(672, 1312);  // snow

		renderer->setTextureAtlas(textureAtlas);
		renderer->setAtlasRects(atlasRects);
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';

		terminate();

		std::exit(vox_errno);
	}
}

void App::renderWorld(const World& world) {
	std::vector<const ChunkRenderState*> transparentStates;
	transparentStates.reserve(world.getRenderStates().size());

	for (const auto& renderState : world.getRenderStates()) {
		if (!world.frustum.isBoxInFrustum(renderState.worldMin, renderState.worldMax))
			continue;

		if (renderState.gpuMesh.uploaded && renderState.gpuMesh.indexCount > 0)
			renderer->renderMesh(renderState.gpuMesh, renderState.worldPosition);

		if (renderState.transparentGpuMesh.uploaded && renderState.transparentGpuMesh.indexCount > 0)
			transparentStates.push_back(&renderState);
	}

	std::ranges::sort(transparentStates, [this](const auto* a, const auto* b) {
		const glm::vec3 centerOffset(ChunkSection::SIZE * 0.5f);
		const float distanceA = glm::length2(a->worldPosition + centerOffset - camera->pos);
		const float distanceB = glm::length2(b->worldPosition + centerOffset - camera->pos);
		return distanceA > distanceB;
	});

	if (!transparentStates.empty()) {
		renderer->beginTransparentPass();
		for (const auto* renderState : transparentStates)
			renderer->renderMesh(renderState->transparentGpuMesh, renderState->worldPosition);
		renderer->endTransparentPass();
	}
}

void App::processMeshUploads(World& world) {
	for (auto& renderState : world.getRenderStates()) {
		if (!renderState.mesh.empty() && !renderState.gpuMesh.uploaded) {
			renderer->uploadMesh(renderState.mesh, renderState.gpuMesh);
			renderState.mesh.clear();
		}

		if (!renderState.transparentMesh.empty() && !renderState.transparentGpuMesh.uploaded) {
			renderer->uploadMesh(renderState.transparentMesh, renderState.transparentGpuMesh);
			renderState.transparentMesh.clear();
		}
	}
}

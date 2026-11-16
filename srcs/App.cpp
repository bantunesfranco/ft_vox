#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include <ranges>

#include "Gamestate.hpp"
#include "glm/gtx/norm.hpp"

#include "ThreadPool.hpp"
#include "PlayerCollision.hpp"
#include "ImGuiUI.hpp"
#include "Protocol.hpp"
#include "RemoteSession.hpp"

#include <charconv>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
		if (y < ChunkColumn::MIN_WORLD_Y)
			return true;
		if (y >= ChunkColumn::MAX_WORLD_Y)
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

// "host" or "host:port" (port defaults to proto::DEFAULT_PORT).
bool parseAddress(const std::string& text, std::string& host, std::uint16_t& port) {
	const auto first = text.find_first_not_of(" \t");
	const auto last  = text.find_last_not_of(" \t");
	if (first == std::string::npos)
		return false;
	const std::string trimmed = text.substr(first, last - first + 1);

	port = proto::DEFAULT_PORT;
	const auto colon = trimmed.rfind(':');
	if (colon == std::string::npos) {
		host = trimmed;
		return true;
	}
	host = trimmed.substr(0, colon);
	unsigned value = 0;
	const char* begin = trimmed.data() + colon + 1;
	const char* end   = trimmed.data() + trimmed.size();
	const auto [ptr, ec] = std::from_chars(begin, end, value);
	if (host.empty() || ec != std::errc{} || ptr != end || value == 0 || value > 65535)
		return false;
	port = static_cast<std::uint16_t>(value);
	return true;
}
}

App::App(const int32_t width, const int32_t height, const char *title, std::map<settings_t, bool>& settings)
	: Engine(width, height, title, settings) {

	const auto concurrency = std::thread::hardware_concurrency();
	auto thread_nr = std::max(1u, concurrency > 2 ? concurrency - 2 : 1u);
	
	setCallbackFunctions();
    loadTextures();
	ui = createImGuiUI(window, guiAtlas, textureAtlas);
	threadPool = std::make_unique<ThreadPool>(thread_nr);
}

void App::setCallbackFunctions() const {
	setErrorCallback(error_callback);
	setKeyCallback(key_callback);
	setCursorPosCallback(mouse_callback);
	setMouseButtonCallback(mouse_button_callback);
	setScrollCallback(scroll_callback);
}

// ============================================================================
//  Game session (singleplayer today, multiplayer later)
// ============================================================================

bool App::startSingleplayer() {
	if (!camera || !threadPool)
		return false;

	endSession();                                   // never two sessions at once
	resetPlayerState();

	// `world`, `*threadPool` and `blockSystem` all outlive the session (it is
	// destroyed in endSession(), before run() ends and in terminate()).
	session = std::make_unique<LocalSession>(world, *threadPool, blockSystem, camera->pos);
	return true;
}

bool App::startMultiplayer(const UiAction& action, const bool hostLocally) {
	if (!camera)
		return false;

	endSession();
	lastSessionError.clear();

	RemoteConfig config;
	config.playerName = action.playerName.empty() ? std::string("Player") : action.playerName;

	if (hostLocally) {
		ServerConfig serverConfig;
		const unsigned cores = std::thread::hardware_concurrency();
		serverConfig.workerThreads = std::max(1u, cores / 4);   // the client keeps its own pool
		hostedServer = std::make_unique<HostedServer>();
		std::string error;
		if (!hostedServer->start(serverConfig, error)) {
			lastSessionError = "Cannot host on port " + std::to_string(serverConfig.port) + ": " + error;
			hostedServer.reset();
			return false;
		}
		config.host = "127.0.0.1";
		config.port = serverConfig.port;
	} else if (!parseAddress(action.address, config.host, config.port)) {
		lastSessionError = "Invalid address: use host or host:port";
		return false;
	}

	resetPlayerState();
	// `world` and `blockSystem` outlive the session (destroyed in endSession()).
	session = std::make_unique<RemoteSession>(world, blockSystem, std::move(config));
	return true;
}

void App::resetPlayerState() {
	spawnInitialized = false;                       // movement waits until the spawn is known
	jumpRequested = false;
	verticalVelocity = 0.0f;
}

void App::endSession() {
	session.reset();              // disconnects first...
	if (hostedServer) {
		hostedServer->stop();     // ...then stops (and joins) a server we were hosting
		hostedServer.reset();
	}
	hasHighlightedBlock = false;
}

// Applies what the session reports. In singleplayer a request is answered before
// it returns (so this is called right after it); in multiplayer the answer
// arrives on a later frame.
void App::applySessionEvents() {
	if (!session)
		return;

	for (const SessionEvent& event : session->drainEvents()) {   // by value: safe even if the session is reset below
		switch (event.type) {
			case SessionEvent::Type::BlockBroken:
				inventory.add(ItemRegistry::fromBlock(event.block));
				break;
			case SessionEvent::Type::BlockPlaced:
				inventory.consumeSelected();
				break;
			case SessionEvent::Type::Disconnected: {
				std::cerr << "Session ended: " << event.reason << '\n';
				const bool wasRemote = session->isRemote();
				lastSessionError = event.reason;
				endSession();
				gameState.returnToTitle();
				if (wasRemote)
					gameState.openMultiplayerMenu();   // back where the player can retry, with the reason
				applyGameState();
				return;                              // the remaining events belonged to the dead session
			}
		}
	}
}

// ============================================================================

// `acceptInput` false: physics only (gravity, falling, landing), no keys. Used
// while a menu is open in multiplayer, where the world does not pause.
void App::updatePlayerMovement(const bool acceptInput) {
	if (!camera)
		return;

	float dt = FPSCounter::getDeltaTime();
	if (dt <= 0.0f)
		dt = 0.0001f;
	dt = glm::min(dt, 0.1f);

	const auto held = [this, acceptInput](const auto key) { return acceptInput && isKeyDown(key); };
	if (!acceptInput)
		jumpRequested = false;

	bool rotated = false;
	if (held(VOX_KEY_UP)) {
		camera->pitch = glm::min(camera->pitch + camera->rotSpeed * dt, 89.0f);
		rotated = true;
	}
	if (held(VOX_KEY_DOWN)) {
		camera->pitch = glm::max(camera->pitch - camera->rotSpeed * dt, -89.0f);
		rotated = true;
	}
	if (held(VOX_KEY_LEFT)) {
		camera->yaw -= camera->rotSpeed * dt;
		rotated = true;
	}
	if (held(VOX_KEY_RIGHT)) {
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
	if (held(VOX_KEY_W)) moveDirection += forward;
	if (held(VOX_KEY_S)) moveDirection -= forward;
	if (held(VOX_KEY_A)) moveDirection -= right;
	if (held(VOX_KEY_D)) moveDirection += right;
	if (flyMode) {
		if (held(VOX_KEY_SPACE)) moveDirection.y += 1.0f;
		if (held(VOX_KEY_LEFT_CONTROL)) moveDirection.y -= 1.0f;
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
    while (windowIsOpen(window))
    {
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
		profiler.beginFrame();
		drawCalls = 0;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwPollEvents();

        FPSCounter::update();

		ui->beginFrame();
		if (bench.active)
			runBenchmarkScript();
		profiler.mark(FrameProfiler::Begin);

		if (gameState.currentState != GameState::START && gameState.currentState != GameState::MULTIPLAYER) {
			// `world` is App's own member, borrowed by the session. It is NOT
			// re-declared here: shadowing it would hide the member that
			// updatePlayerMovement(), renderWorld() and the raycasts use.
			if (session) {
				session->update(camera->pos, FPSCounter::getDeltaTime());   // was: world.updateChunks(...)
				applySessionEvents();
			}

			// Singleplayer pauses in menus. Multiplayer cannot: the player keeps
			// falling / landing and the server keeps getting positions, only
			// input is ignored while a menu is open.
			const bool inGame = gameState.currentState == GameState::IN_GAME;
			const bool simulate = session && (inGame || session->isRemote());
			if (simulate) {
				if (const auto spawn = session->pollSpawn()) {              // was: the getSurfaceHeight block
					camera->pos = *spawn;
					spawnInitialized = true;
					grounded = true;
					jumpRequested = false;
				}
				if (!bench.active)                  // the benchmark flies the camera itself
					updatePlayerMovement(inGame && !ui->wantsKeyboardCapture());
				session->onLocalPlayerMoved(camera->pos, camera->yaw, camera->pitch);
			}
			profiler.mark(FrameProfiler::Session);

			WorldUBO worldUBO{};
			Renderer::initProjectionMatrix(window, camera, worldUBO.MVP);
			world.frustum.updateFrustum(camera->proj, camera->view);

			for (auto& gpuMesh : world.pruneRenderStates())
				renderer->releaseMesh(gpuMesh);
			profiler.mark(FrameProfiler::Prune);
			world.buildRenderStates(camera->pos, camera->dir, *threadPool);
			if (gameState.currentState == GameState::IN_GAME)
				updateBlockHighlight();
			profiler.mark(FrameProfiler::Mesh);

			renderer->beginFrame(worldUBO);
			processMeshUploads(world);
			profiler.mark(FrameProfiler::Upload);
			toggleWireframe(showWireframe);
			renderWorld(world);

			if (gameState.currentState == GameState::IN_GAME) {
				renderBlockHighlight();
			}
			renderRemotePlayers();
			toggleWireframe(false);
			profiler.mark(FrameProfiler::Draw);
		}

		// Field-by-field, as before: designated initializers require the exact
		// declaration order of UiFrameModel, and the old line ended in a comma.
		UiFrameModel uiModel;
		uiModel.state = gameState.currentState;
		uiModel.selectedHotbarSlot = inventory.selectedSlot();
		uiModel.fps = FPSCounter::getFPS();
		if (camera)
			uiModel.cameraPosition = {camera->pos.x, camera->pos.y, camera->pos.z};
		uiModel.showDebug = gameState.currentState == GameState::IN_GAME;
		uiModel.wireframe = showWireframe;
		uiModel.clearColor = clearColor;
		uiModel.lastError = lastSessionError;
		if (session) {
			uiModel.statusText = session->statusText();
			for (const RemotePlayerView& player : session->remotePlayers())
				uiModel.players.push_back(player.name.empty() ? "player " + std::to_string(player.id) : player.name);
			if (gameState.currentState != GameState::START && gameState.currentState != GameState::MULTIPLAYER)
				collectNameTags(uiModel);
		}

		const auto& inventorySlots = inventory.slots();
		for (std::size_t i = 0; i < inventorySlots.size(); ++i) {
			const ItemStack& stack = inventorySlots[i];
			if (!stack.empty())
				uiModel.inventorySlots[i] = {stack.item, stack.count, ItemRegistry::get(stack.item).name};
		}
		const ItemStack& cursorStack = inventory.cursorStack();
		if (!cursorStack.empty())
			uiModel.cursorStack = {
				cursorStack.item,
				cursorStack.count,
				ItemRegistry::get(cursorStack.item).name
			};
		for (const UiAction& action : ui->render(uiModel))
			applyUiAction(action);

		ui->endFrame();
		profiler.mark(FrameProfiler::Ui);

        glfwSwapBuffers(window);
		profiler.mark(FrameProfiler::Swap);
		profiler.endFrame(bench.recording, drawCalls);
    }
	if (bench.active) {
		profiler.report();
		if (Trace::instance().write("bench_trace.json"))
			std::printf("trace written to bench_trace.json (open it in ui.perfetto.dev)\n");
	}

	endSession();             // the session borrows the pool: destroy it first
	threadPool.reset();       // joins the workers (same moment the old local pool was destroyed)
}

void App::destroyBlock() {
	if (!camera || !session)
		return;

	// Same checks as before: look at a block, make sure the player can carry the drop.
	const BlockRaycastHit hit = blockSystem.raycast(camera->pos, camera->dir, world);
	if (!hit.valid)
		return;
	BlockId block = AIR_BLOCK;
	if (!world.getBlockAt(hit.blockPos, block))
		return;
	const ItemId item = ItemRegistry::fromBlock(block);
	if (item == ItemId::Empty || !inventory.canAdd(item))
		return;

	// The session removes the block; the item is added when the BlockBroken event arrives.
	session->requestBreakBlock(hit.blockPos);
	applySessionEvents();     // singleplayer: answered already, so applied now
}

void App::placeBlock() {
	if (!camera || !session)
		return;
	const ItemStack& selected = inventory.selectedStack();
	if (selected.empty())
		return;
	const BlockId block = ItemRegistry::get(selected.item).placedBlock;
	if (block == AIR_BLOCK)
		return;

	// The item is consumed when the BlockPlaced event arrives (only if it really was placed).
	session->requestPlaceBlock(camera->pos, camera->dir, block);
	applySessionEvents();
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


// Other players as boxes the size of the collision box (the eye is EYE_HEIGHT above the feet).
void App::renderRemotePlayers() {
	if (!session)
		return;
	for (const RemotePlayerView& player : session->remotePlayers()) {
		const glm::vec3 feet = player.eyePos - glm::vec3(0.0f, PlayerCollision::EYE_HEIGHT, 0.0f);
		const glm::vec3 half(PlayerCollision::HALF_WIDTH, 0.0f, PlayerCollision::HALF_WIDTH);
		renderer->renderBoundingBox(feet - half, feet + half + glm::vec3(0.0f, PlayerCollision::HEIGHT, 0.0f));
	}
}

// Projects a point just above each other player's head with this frame's
// camera matrices. Players behind the camera, off screen or too far are left out.
void App::collectNameTags(UiFrameModel& model) const {
	if (!session || !camera)
		return;
	constexpr float MAX_DISTANCE = 64.0f;
	constexpr float ABOVE_HEAD   = 0.5f;
	const glm::mat4 viewProj = camera->proj * camera->view;

	for (const RemotePlayerView& player : session->remotePlayers()) {
		const glm::vec3 anchor = player.eyePos
			+ glm::vec3(0.0f, PlayerCollision::HEIGHT - PlayerCollision::EYE_HEIGHT + ABOVE_HEAD, 0.0f);
		const float distance = glm::length(anchor - camera->pos);
		if (distance > MAX_DISTANCE)
			continue;

		const glm::vec4 clip = viewProj * glm::vec4(anchor, 1.0f);
		if (clip.w <= 0.05f)
			continue;                                   // behind the camera
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f || ndc.z > 1.0f)
			continue;

		UiNameTag tag;
		tag.name     = player.name.empty() ? "player " + std::to_string(player.id) : player.name;
		tag.ndcX     = ndc.x;
		tag.ndcY     = ndc.y;
		tag.distance = distance;
		model.nameTags.push_back(std::move(tag));
	}
	// Far first, so near tags are drawn on top.
	std::ranges::sort(model.nameTags, [](const UiNameTag& a, const UiNameTag& b) { return a.distance > b.distance; });
}

// ============================================================================
//  Benchmark (ft_vox --bench [seconds])
//
//  Starts singleplayer, turns vsync off and flies a fixed, time-based path so
//  runs are comparable: BENCH_WARMUP seconds hovering and turning at spawn
//  (terrain streams in, not recorded), then flying +X at BENCH_SPEED while
//  turning (recorded: streaming, meshing and drawing all at once).
// ============================================================================

void App::enableBenchmark(const double seconds) {
	bench.active   = true;
	bench.duration = std::max(seconds, BENCH_WARMUP + 1.0);
	glfwSwapInterval(0);
	Trace::instance().enable();
	Trace::instance().setMainThread();
}

void App::runBenchmarkScript() {
	if (gameState.currentState == GameState::START) {
		applyUiAction({UiActionType::START_GAME, false, {}});
		flyMode = true;
		bench.start = glfwGetTime();
		return;
	}
	const double t = glfwGetTime() - bench.start;
	if (t >= bench.duration) {
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		return;
	}
	// One screenshot when loading is done, before moving: a correctness check
	// to compare between builds (taken from the previous frame's back buffer).
	if (t >= BENCH_WARMUP && !bench.screenshotTaken) {
		saveScreenshot("bench_shot.png");
		bench.screenshotTaken = true;
		return;                                // this frame is not recorded (PNG encoding)
	}
	bench.recording = t >= BENCH_WARMUP;

	const double flying = std::max(0.0, t - BENCH_WARMUP);
	camera->pos   = glm::vec3(8.0f + static_cast<float>(flying * BENCH_SPEED), 110.0f, 8.0f);
	// Turning (a full turn every ~14 s), except during the last second of the
	// warm-up so the screenshot is always taken from exactly the same pose.
	const double turning = t < BENCH_WARMUP ? std::min(t, BENCH_WARMUP - 1.0) : t - 1.0;
	camera->yaw   = static_cast<float>(turning * 25.0);
	camera->pitch = -20.0f;
	const float yaw = glm::radians(camera->yaw);
	const float pitch = glm::radians(camera->pitch);
	camera->dir = glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch),
	                                       std::sin(yaw) * std::cos(pitch)));
}

void App::saveScreenshot(const char* path) const {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	if (width <= 0 || height <= 0)
		return;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_FRONT);                    // the frame shown last
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
	glReadBuffer(GL_BACK);
	stbi_flip_vertically_on_write(1);         // GL rows start at the bottom
	if (stbi_write_png(path, width, height, 3, pixels.data(), width * 3))
		std::printf("screenshot written to %s\n", path);
}

void App::terminate() {
	endSession();             // before anything it borrows is torn down
	if (ui)
		ui->shutdown();
    Engine::terminate();
}

void App::applyGameState() {
	glfwSetInputMode(window, GLFW_CURSOR,
		gameState.currentState == GameState::IN_GAME ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void App::togglePauseMenu() {
	const bool wasInventory = gameState.currentState == GameState::INVENTORY;
	gameState.togglePause();
	if (wasInventory)
		inventory.returnCursorStack();
	applyGameState();
}

void App::toggleInventoryMenu() {
	const bool wasInventory = gameState.currentState == GameState::INVENTORY;
	gameState.toggleInventory();
	if (wasInventory)
		inventory.returnCursorStack();
	applyGameState();
}

void App::selectHotbarSlot(const uint16_t slot) {
	inventory.selectHotbarSlot(slot);
}

void App::cycleHotbar(const int delta) {
	inventory.cycleHotbar(delta);
}

void App::clickInventorySlot(const int slot, const bool secondaryClick) {
	if (slot >= 0)
		inventory.clickSlot(static_cast<std::size_t>(slot), secondaryClick);
}

uint16_t App::selectedHotbarSlot() const {
	return inventory.selectedSlot();
}

void App::toggleWireframeDebug() {
	showWireframe = !showWireframe;
}

bool App::isInGame() const {
	return gameState.currentState == GameState::IN_GAME;
}

bool App::wantsMouseCapture() const {
	return ui && ui->wantsMouseCapture();
}

bool App::wantsKeyboardCapture() const {
	return ui && ui->wantsKeyboardCapture();
}

void App::applyUiAction(const UiAction& action) {
	switch (action.type) {
		case UiActionType::START_GAME:
			if (!startSingleplayer())          // creates the LocalSession
				break;
			gameState.startGameplay();
			applyGameState();
			break;
		case UiActionType::START_GAME_MP:
			lastSessionError.clear();
			gameState.openMultiplayerMenu();
			applyGameState();
			break;
		case UiActionType::HOST_MP:
		case UiActionType::JOIN_MP:
			if (!startMultiplayer(action, action.type == UiActionType::HOST_MP))
				break;                         // lastSessionError says why; stay on the menu
			gameState.startGameplay();
			applyGameState();
			break;
		case UiActionType::RESUME_GAME:
			gameState.togglePause();
			applyGameState();
			break;
		case UiActionType::RETURN_TO_TITLE:
			endSession();                      // disconnects (and stops a hosted server)
			gameState.returnToTitle();
			applyGameState();
			break;
		case UiActionType::QUIT_GAME:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			break;
		case UiActionType::SET_WIREFRAME:
			showWireframe = action.enabled;
			break;
		case UiActionType::SET_CLEAR_COLOR:
			clearColor = action.color;
			break;
		case UiActionType::INVENTORY_SLOT_CLICK:
			clickInventorySlot(action.slot, action.secondaryClick);
			break;
		default: throw std::runtime_error("Unknown action type");
	}
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
		atlasRects[8] = spriteRect(704, 240);   // bedrock

		renderer->setTextureAtlas(textureAtlas);
		renderer->setAtlasRects(atlasRects);

		guiAtlas = loadTexture("./textures/minecraft_gui_atlas.png");
		glTextureParameteri(guiAtlas, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(guiAtlas, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(guiAtlas, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(guiAtlas, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';

		terminate();

		std::exit(vox_errno);
	}
}

// Two multi-draw calls per frame: every visible opaque mesh, then every
// visible transparent one back to front. The vectors are members so their
// storage is reused frame to frame.
void App::renderWorld(const World& _world) {
	opaqueBatch.clear();
	transparentSort.clear();

	const glm::vec3 centerOffset(ChunkSection::SIZE * 0.5f);
	for (const auto& renderState : _world.getRenderStates()) {
		if (!_world.frustum.isBoxInFrustum(renderState.worldMin, renderState.worldMax))
			continue;

		if (renderState.gpuMesh.uploaded && renderState.gpuMesh.indexCount > 0)
			opaqueBatch.push_back({&renderState.gpuMesh, renderState.worldPosition});

		if (renderState.transparentGpuMesh.uploaded && renderState.transparentGpuMesh.indexCount > 0) {
			const float distance = glm::length2(renderState.worldPosition + centerOffset - camera->pos);
			transparentSort.emplace_back(distance, DrawItem{&renderState.transparentGpuMesh, renderState.worldPosition});
		}
	}

	renderer->drawBatch(opaqueBatch);
	drawCalls += opaqueBatch.size();

	if (transparentSort.empty())
		return;
	std::ranges::sort(transparentSort, [](const auto& a, const auto& b) { return a.first > b.first; });
	transparentBatch.clear();
	for (const auto& [distance, item] : transparentSort)
		transparentBatch.push_back(item);

	renderer->beginTransparentPass();
	renderer->drawBatch(transparentBatch);
	renderer->endTransparentPass();
	drawCalls += transparentBatch.size();
}

void App::processMeshUploads(World& _world) {
	for (auto& renderState : _world.getRenderStates()) {
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
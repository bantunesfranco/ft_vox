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
#include "BlockTextures.hpp"
#include "Protocol.hpp"
#include "PlayerPhysics.hpp"
#include "PlayerStore.hpp"
#include "RemoteSession.hpp"

#include <charconv>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
bool collidesWithWorld(const glm::vec3& eyePosition, const World& world) {
	// Below the world is solid, above it empty; unloaded blocks count as solid.
	const BlockRegistry& registry = world.getBlockRegistry();
	return PlayerPhysics::boxHitsShapes(eyePosition, [&world, &registry](const glm::ivec3& p) {
		if (p.y < ChunkColumn::MIN_WORLD_Y)
			return 0;
		if (p.y >= ChunkColumn::MAX_WORLD_Y)
			return -1;
		BlockId block = AIR_BLOCK;
		if (!world.getBlockAt(p, block))
			return 0;
		const BlockDefinition& def = registry.get(block);
		return def.solid ? static_cast<int>(def.collisionInset) : -1;
	});
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

// Plays `info`: takes the world's lock, then streams its saved / generated
// columns through a RegionStore in the world folder.
bool App::startSingleplayer(const WorldInfo& info) {
	if (!camera || !threadPool)
		return false;

	endSession();                                   // never two sessions at once
	if (!info.readable) {
		lastSessionError = "'" + info.name + "' cannot be read: its level.bin is damaged";
		return false;
	}
	std::string error;
	auto lock = WorldLock::acquire(info.folder, error);
	if (!lock) {
		lastSessionError = "Cannot open '" + info.name + "': " + error;
		return false;
	}
	WorldInfo played = info;
	if (!markPlayed(played, error))
		std::cerr << "Could not update " << info.name << ": " << error << '\n';

	resetPlayerState();
	playingWorld = played;
	timeTicks = played.timeOfDay;
	lastSessionError.clear();
	worldLock = std::move(lock);
	regionStore = std::make_shared<RegionStore>(info.folder / "region");
	world.open(info.seed, regionStore);
	playingSeed = info.seed;

	// `world`, `*threadPool` and `blockSystem` all outlive the session (it is
	// destroyed in endSession(), before run() ends and in terminate()).
	const glm::ivec2 spawn = world.spawnPoint();
	session = std::make_unique<LocalSession>(world, *threadPool, blockSystem,
		glm::vec3(static_cast<float>(spawn.x) + 0.5f, 0.0f, static_cast<float>(spawn.y) + 0.5f),
		singleplayerFile(info.folder));
	return true;
}

// The benchmark plays a fixed seed and never saves.
void App::startBenchmarkWorld() {
	endSession();
	resetPlayerState();
	world.open(BENCH_SEED, nullptr);
	playingSeed = BENCH_SEED;
	const int ground = world.generatedSurfaceHeight(static_cast<int>(bench.startXZ.x), static_cast<int>(bench.startXZ.y));
	bench.height = static_cast<float>(std::max(ground, 62) + 35);
	timeTicks = bench.ticks;
	session = std::make_unique<LocalSession>(world, *threadPool, blockSystem,
		glm::vec3(bench.startXZ.x, bench.height, bench.startXZ.y), std::nullopt);
	gameState.startGameplay();
	applyGameState();
}

void App::refreshWorlds() {
	const std::string selectedFolder = selectedWorld >= 0 && selectedWorld < static_cast<int>(worlds.size())
		? worlds[static_cast<std::size_t>(selectedWorld)].folder.string() : std::string{};
	worlds = listWorlds(savesDir);
	selectedWorld = worlds.empty() ? -1 : 0;
	for (std::size_t i = 0; i < worlds.size(); ++i)
		if (worlds[i].folder.string() == selectedFolder)
			selectedWorld = static_cast<int>(i);
}

bool App::startMultiplayer(const UiAction& action, const bool hostLocally) {
	if (!camera)
		return false;

	endSession();
	lastSessionError.clear();

	RemoteConfig config;
	config.playerName = action.text2.empty() ? std::string("Player") : action.text2;

	if (hostLocally) {
		// Hosts the world selected in the world list (endSession() above has
		// already saved it and released its lock, so the server can take it).
		if (selectedWorld < 0 || selectedWorld >= static_cast<int>(worlds.size())
		    || !worlds[static_cast<std::size_t>(selectedWorld)].readable) {
			lastSessionError = "Choose a world to host in Singleplayer first";
			return false;
		}
		ServerConfig serverConfig;
		const unsigned cores = std::thread::hardware_concurrency();
		serverConfig.workerThreads = std::max(1u, cores / 4);   // the client keeps its own pool
		serverConfig.worldFolder = worlds[static_cast<std::size_t>(selectedWorld)].folder;
		hostedServer = std::make_unique<HostedServer>();
		std::string error;
		if (!hostedServer->start(serverConfig, error)) {
			lastSessionError = "Cannot host on port " + std::to_string(serverConfig.port) + ": " + error;
			hostedServer.reset();
			return false;
		}
		config.host = "127.0.0.1";
		config.port = serverConfig.port;
	} else if (!parseAddress(action.text1, config.host, config.port)) {
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
	velocity = glm::vec3(0.0f);
	fluidFog = glm::vec4(0.0f);
}

void App::endSession() {
	session.reset();              // disconnects first...
	if (playingWorld) {
		playingWorld->timeOfDay = static_cast<std::uint32_t>(timeTicks) % TimeOfDay::TICKS_PER_DAY;
		std::string error;
		if (!markPlayed(*playingWorld, error))
			std::cerr << "Could not save the time of " << playingWorld->name << ": " << error << '\n';
		playingWorld.reset();
	}
	if (regionStore) {
		if (threadPool)
			world.flush(*threadPool);   // every edit and generated column on disk
		world.open(0, nullptr);         // forget it: the next world starts empty
		regionStore.reset();
	}
	worldLock.reset();
	playingSeed.reset();
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
			case SessionEvent::Type::BlockPlaced:
				break;                               // the session has already updated the inventory
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
	if (rotated)
		camera->dir = lookDirection(camera->yaw, camera->pitch);

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
		// Minecraft's flight keys: Space up, Shift down, Ctrl fast.
		if (held(VOX_KEY_SPACE)) moveDirection.y += 1.0f;
		if (held(VOX_KEY_LEFT_SHIFT)) moveDirection.y -= 1.0f;
		const float speed = camera->moveSpeed * (held(VOX_KEY_LEFT_CONTROL) ? 10.0f : 1.0f);
		if (glm::dot(moveDirection, moveDirection) > 0.0f)
			camera->pos += glm::normalize(moveDirection) * speed * dt;
		velocity = glm::vec3(0.0f);
		jumpRequested = false;
		return;
	}

	PlayerPhysics::Input input;
	if (glm::dot(moveDirection, moveDirection) > 0.0f) {
		const glm::vec3 wish = glm::normalize(moveDirection);
		input.wish = {wish.x, wish.z};
	}
	input.jump = jumpRequested || held(VOX_KEY_SPACE);       // held: jumps again on landing
	// Minecraft's keys: Shift sneaks; Ctrl or a double-tapped W sprints.
	input.sneak = held(VOX_KEY_LEFT_SHIFT);
	input.sprint = sprint.update(held(VOX_KEY_W), held(VOX_KEY_LEFT_CONTROL), input.sneak);
	jumpRequested = false;

	PlayerPhysics::Body body{camera->pos, velocity, grounded};
	PlayerPhysics::step(body, input, dt, [this](const glm::vec3& eye) { return collidesWithWorld(eye, world); });
	camera->pos = body.eye;
	velocity = body.velocity;
	grounded = body.grounded;
}

void App::simulate() {
	// `world` is App's own member, borrowed by the session. It is NOT
	// re-declared here: shadowing it would hide the member that
	// updatePlayerMovement(), renderWorld() and the raycasts use.
	if (session) {
		session->update(camera->pos, FPSCounter::getDeltaTime());
		applySessionEvents();
	}

	// Singleplayer pauses in menus. Multiplayer cannot: the player keeps
	// falling / landing and the server keeps getting positions, only
	// input is ignored while a menu is open.
	const bool inGame = gameState.currentState == GameState::IN_GAME;
	const bool simulating = session && (inGame || session->isRemote());
	if (simulating) {
		if (const auto serverTime = session->pollTimeOfDay())
			timeTicks = *serverTime;                                 // the server's clock wins
		if (const auto spawn = session->pollSpawn()) {
			camera->pos = spawn->eye;
			camera->yaw = spawn->yaw;
			camera->pitch = spawn->pitch;
			camera->dir = lookDirection(camera->yaw, camera->pitch);
			spawnInitialized = true;
			grounded = true;
			jumpRequested = false;
		}
		if (!bench.active) {                // the benchmark flies the camera itself, at a fixed time
			updatePlayerMovement(inGame && !ui->wantsKeyboardCapture());
			timeTicks = std::fmod(timeTicks + FPSCounter::getDeltaTime() * TimeOfDay::TICKS_PER_SECOND,
			                      static_cast<double>(TimeOfDay::TICKS_PER_DAY));
		}
		session->onLocalPlayerMoved(camera->pos, camera->yaw, camera->pitch);
	}
	profiler.mark(FrameProfiler::Session);
}

void App::renderFrame() {
	WorldUBO worldUBO{};
	Renderer::initProjectionMatrix(window, camera, worldUBO.MVP);
	updateFluidFog();
	worldUBO.eye = glm::vec4(camera->pos, 1.0f);
	worldUBO.fog = fluidFog;
	// Time of day: sky light, sky colours; the haze is the horizon colour.
	const auto ticks = static_cast<std::uint32_t>(timeTicks);
	worldUBO.time.x = TimeOfDay::daylight(ticks);
	const TimeOfDay::SkyColours sky = TimeOfDay::skyColours(ticks, {clearColor[0], clearColor[1], clearColor[2]});
	const glm::vec3 zenith = sky.zenith;
	const glm::vec3 horizon = sky.horizon;
	worldUBO.skyFog = glm::vec4(horizon, static_cast<float>(World::CHUNK_RADIUS * ChunkSection::SIZE));
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
	if (fluidFog.w <= 0.0f)                    // under water the fog colour is the sky
		renderer->drawSky(glm::inverse(camera->proj * glm::mat4(glm::mat3(camera->view))), zenith, horizon,
		                  TimeOfDay::sunDirection(ticks));
	toggleWireframe(showWireframe);
	renderWorld(world);

	if (gameState.currentState == GameState::IN_GAME) {
		renderBlockHighlight();
	}
	renderRemotePlayers();
	toggleWireframe(false);
	profiler.mark(FrameProfiler::Draw);
}

UiFrameModel App::buildUiModel() const {
	// Field-by-field: designated initializers would require the exact
	// declaration order of UiFrameModel.
	UiFrameModel uiModel;
	uiModel.state = gameState.currentState;
	uiModel.selectedHotbarSlot = selectedHotbarSlot();
	uiModel.fps = FPSCounter::getFPS();
	if (camera)
		uiModel.cameraPosition = {camera->pos.x, camera->pos.y, camera->pos.z};
	uiModel.showDebug = gameState.currentState == GameState::IN_GAME;
	uiModel.wireframe = showWireframe;
	uiModel.clearColor = clearColor;
	uiModel.timeOfDay = static_cast<int>(timeTicks);
	uiModel.lastError = lastSessionError;
	if (playingSeed)
		uiModel.seedText = std::to_string(static_cast<std::int64_t>(*playingSeed));
	if (gameState.currentState == GameState::MULTIPLAYER
	    && selectedWorld >= 0 && selectedWorld < static_cast<int>(worlds.size()))
		uiModel.hostWorldName = worlds[static_cast<std::size_t>(selectedWorld)].name;
	if (gameState.currentState == GameState::WORLD_SELECT) {
		for (const WorldInfo& info : worlds) {
			UiWorldEntry entry;
			entry.name = info.name;
			entry.readable = info.readable;
			entry.detail = info.folder.filename().string()
				+ (info.readable ? "    seed " + std::to_string(static_cast<std::int64_t>(info.seed))
				                 : "    (cannot be read)");
			uiModel.worlds.push_back(std::move(entry));
		}
		uiModel.selectedWorld = selectedWorld;
	}
	if (session) {
		uiModel.statusText = session->statusText();
		for (const RemotePlayerView& player : session->remotePlayers())
			uiModel.players.push_back(player.name.empty() ? "player " + std::to_string(player.id) : player.name);
		if (!isMenuState(gameState.currentState))
			collectNameTags(uiModel);
	}

	if (!session)
		return uiModel;                          // no player: the slots stay empty
	const Inventory& inventory = session->inventory();
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
	return uiModel;
}

void App::run() {
    while (windowIsOpen(window))
    {
        // Inside water (or lava) the sky is the fog colour too.
        if (fluidFog.w > 0.0f)
            glClearColor(fluidFog.r, fluidFog.g, fluidFog.b, 1.0f);
        else
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

		if (!isMenuState(gameState.currentState)) {
			simulate();
			renderFrame();
		}

		for (const UiAction& action : ui->render(buildUiModel()))
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
	threadPool.reset();       // joins the workers
}

void App::destroyBlock() {
	if (!camera || !session)
		return;

	const BlockRaycastHit hit = blockSystem.raycast(camera->pos, camera->dir, world);
	if (!hit.valid)
		return;

	// The session refuses a block whose item would not fit, removes it and adds the item.
	session->requestBreakBlock(hit.blockPos);
	applySessionEvents();     // singleplayer: answered already, so applied now
}

void App::placeBlock() {
	if (!camera || !session)
		return;
	// The session places its selected slot's block and consumes it (only if it really was placed).
	session->requestPlaceBlock(camera->pos, camera->dir);
	applySessionEvents();
}

void App::forwardPressed() {
	sprint.forwardPressed(glfwGetTime());
}

void App::toggleFlyMode() {
	flyMode = !flyMode;
	velocity = glm::vec3(0.0f);
	jumpRequested = false;
}

void App::jump() {
	if (!flyMode)
		jumpRequested = true;
}

// Fog of the block the camera's eye is in (water: blue), for the shader and the sky.
void App::updateFluidFog() {
	fluidFog = glm::vec4(0.0f);
	BlockId eyeBlock = AIR_BLOCK;
	if (!camera || !world.getBlockAt(glm::ivec3(glm::floor(camera->pos)), eyeBlock))
		return;
	const BlockDefinition& block = world.getBlockRegistry().get(eyeBlock);
	if (block.fogDistance <= 0.0f)
		return;
	const auto channel = [&block](const int shift) {
		return static_cast<float>((block.fogColor >> shift) & 0xFFu) / 255.0f;
	};
	fluidFog = glm::vec4(channel(16), channel(8), channel(0), block.fogDistance);
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

	// Outline the block's own shape (a torch is a thin stick), not its whole cell.
	BlockId id = AIR_BLOCK;
	if (!world.getBlockAt(highlightedBlockPos, id))
		return;
	const BlockBox box = outlineBox(world.getBlockRegistry().get(id));
	const glm::vec3 origin(highlightedBlockPos);
	renderer->renderBoundingBox(origin + glm::vec3(box.min) / 16.0f, origin + glm::vec3(box.max) / 16.0f);
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

void App::enableBenchmark(const double seconds, const glm::vec2 startXZ, const std::uint32_t ticks) {
	bench.startXZ = startXZ;
	bench.ticks = ticks % TimeOfDay::TICKS_PER_DAY;
	bench.active   = true;
	bench.duration = std::max(seconds, BENCH_WARMUP + 1.0);
	glfwSwapInterval(0);
	Trace::instance().enable();
	Trace::instance().setMainThread();
}

void App::runBenchmarkScript() {
	if (gameState.currentState == GameState::START) {
		startBenchmarkWorld();
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
	camera->pos   = glm::vec3(bench.startXZ.x + static_cast<float>(flying * BENCH_SPEED), bench.height, bench.startXZ.y);
	// Turning (a full turn every ~14 s), except during the last second of the
	// warm-up so the screenshot is always taken from exactly the same pose.
	const double turning = t < BENCH_WARMUP ? std::min(t, BENCH_WARMUP - 1.0) : t - 1.0;
	camera->yaw   = static_cast<float>(turning * 25.0);
	camera->pitch = -20.0f;
	camera->dir   = lookDirection(camera->yaw, camera->pitch);
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
	if (wasInventory && session)
		session->inventoryAction({InventoryAction::Kind::Close, 0, false});   // the cursor stack goes back
	applyGameState();
}

void App::toggleInventoryMenu() {
	const bool wasInventory = gameState.currentState == GameState::INVENTORY;
	gameState.toggleInventory();
	if (wasInventory && session)
		session->inventoryAction({InventoryAction::Kind::Close, 0, false});   // the cursor stack goes back
	applyGameState();
}

void App::selectHotbarSlot(const uint16_t slot) {
	if (session && slot < Inventory::HOTBAR_SIZE)
		session->inventoryAction({InventoryAction::Kind::Select, static_cast<std::uint8_t>(slot), false});
}

// Same arithmetic as Inventory::cycleHotbar, sent as a selection.
void App::cycleHotbar(const int delta) {
	if (!session)
		return;
	const int size = static_cast<int>(Inventory::HOTBAR_SIZE);
	const int next = (static_cast<int>(session->inventory().selectedSlot()) + delta % size + size) % size;
	selectHotbarSlot(static_cast<uint16_t>(next));
}

void App::clickInventorySlot(const int slot, const bool secondaryClick) {
	if (session && slot >= 0 && slot < static_cast<int>(Inventory::SLOT_COUNT))
		session->inventoryAction({InventoryAction::Kind::Click, static_cast<std::uint8_t>(slot), secondaryClick});
}

uint16_t App::selectedHotbarSlot() const {
	return session ? session->inventory().selectedSlot() : 0;
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
		case UiActionType::OPEN_WORLDS:
			if (gameState.currentState == GameState::START)
				lastSessionError.clear();
			refreshWorlds();
			gameState.openMenu(GameState::WORLD_SELECT);
			break;
		case UiActionType::SELECT_WORLD:
			if (action.slot >= 0 && action.slot < static_cast<int>(worlds.size()))
				selectedWorld = action.slot;
			break;
		case UiActionType::PLAY_WORLD:
			if (action.slot < 0 || action.slot >= static_cast<int>(worlds.size()))
				break;
			if (!startSingleplayer(worlds[static_cast<std::size_t>(action.slot)]))
				break;                         // lastSessionError says why; stay on the list
			gameState.startGameplay();
			applyGameState();
			break;
		case UiActionType::OPEN_CREATE_WORLD:
			lastSessionError.clear();
			gameState.openMenu(GameState::WORLD_CREATE);
			break;
		case UiActionType::CREATE_WORLD: {
			std::string error;
			const auto created = createWorld(savesDir, action.text1, action.text2, error);
			if (!created) {
				lastSessionError = "Cannot create the world: " + error;
				break;
			}
			refreshWorlds();
			if (!startSingleplayer(*created)) {
				gameState.openMenu(GameState::WORLD_SELECT);
				break;
			}
			gameState.startGameplay();
			applyGameState();
			break;
		}
		case UiActionType::DELETE_WORLD: {
			if (action.slot < 0 || action.slot >= static_cast<int>(worlds.size()))
				break;
			std::string error;
			if (!deleteWorld(worlds[static_cast<std::size_t>(action.slot)], error))
				lastSessionError = "Cannot delete the world: " + error;
			refreshWorlds();
			break;
		}
		case UiActionType::START_GAME_MP:
			lastSessionError.clear();
			refreshWorlds();                   // for "Host": which world would be hosted
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
		case UiActionType::SET_TIME:
			timeTicks = static_cast<double>(static_cast<std::uint32_t>(action.slot) % TimeOfDay::TICKS_PER_DAY);
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
		const auto spriteRect = [atlasSize](const BlockTextures::PixelRect px) {
			return glm::vec4(static_cast<float>(px.x) / atlasSize, static_cast<float>(px.y) / atlasSize,
			                 static_cast<float>(px.w) / atlasSize, static_cast<float>(px.h) / atlasSize);
		};

		std::array<glm::vec4, 64> atlasRects{};
		atlasRects.fill(spriteRect(BlockTextures::pixels(0)));
		std::vector<std::uint8_t> cell(BlockTextures::CELL * BlockTextures::CELL * 4);
		for (std::uint16_t i = 0; i < BlockTextures::TEXTURE_COUNT; ++i) {
			const BlockTextures::Sprite& sprite = BlockTextures::sprite(i);
			atlasRects[i] = spriteRect(BlockTextures::pixels(i));
			if (sprite.tint == BlockTextures::NO_TINT)
				continue;
			// Grey sprites (leaves): multiply the whole cell by the tint, once.
			glGetTextureSubImage(textureAtlas, 0, sprite.x, sprite.y, 0, BlockTextures::CELL, BlockTextures::CELL, 1,
			                     GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(cell.size()), cell.data());
			const std::uint32_t channel[3] = {sprite.tint >> 16, (sprite.tint >> 8) & 0xFFu, sprite.tint & 0xFFu};
			for (std::size_t p = 0; p < cell.size(); p += 4)
				for (std::size_t c = 0; c < 3; ++c)
					cell[p + c] = static_cast<std::uint8_t>(cell[p + c] * channel[c] / 255u);
			glTextureSubImage2D(textureAtlas, 0, sprite.x, sprite.y, BlockTextures::CELL, BlockTextures::CELL,
			                    GL_RGBA, GL_UNSIGNED_BYTE, cell.data());
		}
		glGenerateTextureMipmap(textureAtlas);

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
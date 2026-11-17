#ifndef APP_HPP
#define APP_HPP

#include <array>
#include <cstdint>

#include "Block.hpp"
#include "BlockSystem.hpp"
#include "Engine.hpp"
#include "Gamestate.hpp"
#include "Inventory.hpp"
#include "UI.hpp"
#include "World.hpp"
#include "GameSession.hpp"
#include "GameServer.hpp"
#include "FrameProfiler.hpp"
#include "RegionStore.hpp"
#include "WorldStore.hpp"
#include <filesystem>
#include <optional>
#include <string>

class App : public Engine
{
	public:
		App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool>& settings);
		// Ends any session first: it borrows `world`, which is declared after it
		// and would otherwise be destroyed before it.
		~App() override { endSession(); }
		App(const App&) = delete;
		App& operator=(const App&) = delete;

		void	run() override;
		// ft_vox --bench: scripted singleplayer flight with a timing report.
		void	enableBenchmark(double seconds, glm::vec2 startXZ = {8.0f, 8.0f});

		void	terminate() override;
		void	destroyBlock();
		void	placeBlock();
		void	toggleSpeedBoost();
		void	toggleFlyMode();
		void	jump();

		void togglePauseMenu();
		void toggleInventoryMenu();
		void selectHotbarSlot(uint16_t slot);
		void cycleHotbar(int delta);
		void clickInventorySlot(int slot, bool secondaryClick);
		uint16_t selectedHotbarSlot() const;
		void toggleWireframeDebug();
		bool isInGame() const;
		bool wantsMouseCapture() const;
		bool wantsKeyboardCapture() const;

	private:
		void	setCallbackFunctions() const;

		bool    startSingleplayer(const WorldInfo& info);
		void    startBenchmarkWorld();
		void    refreshWorlds();
		bool    startMultiplayer(const UiAction& action, bool hostLocally);
		void    resetPlayerState();
		void    endSession();
		void    applySessionEvents();

		void	loadTextures();
		void	applyGameState();
		void	applyUiAction(const UiAction& action);

		// Declared before `session`: a hosted server outlives the session that
		// connects to it (endSession() also tears them down in that order).
		std::unique_ptr<HostedServer> hostedServer;
		std::unique_ptr<GameSession> session;
		std::string lastSessionError;

		// Worlds on disk and the one being played (singleplayer).
		std::filesystem::path savesDir{"saves"};
		std::vector<WorldInfo> worlds;
		int selectedWorld = -1;
		std::unique_ptr<WorldLock> worldLock;
		std::shared_ptr<RegionStore> regionStore;
		std::optional<std::uint64_t> playingSeed;
		std::unique_ptr<UI> ui;
		std::unique_ptr<ThreadPool> threadPool;

		GameStateController gameState;
		Inventory inventory;
		World world;
		BlockSystem blockSystem;

		bool showWireframe{false};
		GLuint textureAtlas{0};
		GLuint guiAtlas{0};
		bool speedBoostEnabled = false;
		bool flyMode = false;
		bool grounded = false;
		bool spawnInitialized = false;
		bool jumpRequested = false;
		glm::vec3 velocity{0.0f};          // walking physics (PlayerPhysics), unused when flying
		std::array<float, 4> clearColor{0.075f, 0.33f, 0.61f, 1.0f};
		bool hasHighlightedBlock = false;
		glm::ivec3 highlightedBlockPos{};

		void	renderWorld(const World& _world);
		void	processMeshUploads(World& _world);
		void	updatePlayerMovement(bool acceptInput);
		void	updateBlockHighlight();
		void	renderBlockHighlight();
		void	renderRemotePlayers();
		void	runBenchmarkScript();
		void	saveScreenshot(const char* path) const;

		static constexpr double BENCH_WARMUP = 8.0;     // seconds, not recorded
		static constexpr double BENCH_SPEED  = 20.0;    // blocks per second
		static constexpr std::uint64_t BENCH_SEED = 20261116;   // fixed: runs stay comparable
		struct BenchmarkState {
			bool   active    = false;
			bool   recording = false;
			bool   screenshotTaken = false;
			double duration  = 30.0;
			double start     = 0.0;
			glm::vec2 startXZ{8.0f, 8.0f};
			float  height    = 110.0f;   // eye height of the flight
		} bench;
		FrameProfiler profiler;
		std::size_t   drawCalls = 0;   // meshes drawn this frame (in 2 multi-draw calls)

		// renderWorld() scratch, kept to reuse the allocations every frame.
		std::vector<DrawItem> opaqueBatch;
		std::vector<DrawItem> transparentBatch;
		std::vector<std::pair<float, DrawItem>> transparentSort;
		void	collectNameTags(UiFrameModel& model) const;

};

void error_callback(int error, const char* description);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

#endif

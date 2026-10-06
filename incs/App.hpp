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

class App : public Engine
{
	public:
		App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool>& settings);
		~App() override = default;
		App(const App&) = delete;
		App& operator=(const App&) = delete;

		void	run() override;

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
		void	loadTextures();
		void	applyGameState();
		void	applyUiAction(const UiAction& action);

		World world;
		BlockSystem blockSystem;
	Inventory inventory;
		std::unique_ptr<UI> ui;
		GameStateController gameState;
		bool showWireframe{false};
		GLuint textureAtlas{0};
	GLuint guiAtlas{0};
		bool speedBoostEnabled = false;
		bool flyMode = false;
		bool grounded = false;
		bool spawnInitialized = false;
		bool jumpRequested = false;
		float verticalVelocity = 0.0f;
		std::array<float, 4> clearColor{0.075f, 0.33f, 0.61f, 1.0f};
		bool hasHighlightedBlock = false;
		glm::ivec3 highlightedBlockPos{};

		void	renderWorld(const World& world);
		void	processMeshUploads(World& world);
		void	updatePlayerMovement();
		void	updateBlockHighlight();
		void	renderBlockHighlight();

};

void error_callback(int error, const char* description);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

#endif

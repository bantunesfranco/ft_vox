
#ifndef GAMESTATE_HPP
#define GAMESTATE_HPP

#include "GameMode.hpp"


class GameStateController {
public:
	GameStateController() = default;
    void toggleInventory();
    void togglePause();
    void startGameplay();
	void openMultiplayerMenu();
	// Switches between title-screen menus (only from a menu, only to a menu).
	void openMenu(GameState menu);
	void returnToTitle();

    GameState currentState{GameState::START};

private:
	GameState resumeState{GameState::IN_GAME};
};

#endif // GAMESTATE_HPP

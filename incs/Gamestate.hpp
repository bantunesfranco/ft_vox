
#ifndef GAMESTATE_HPP
#define GAMESTATE_HPP

#include "GameMode.hpp"


class GameStateController {
public:
	GameStateController() = default;
    void toggleInventory();
    void togglePause();
    void startGameplay();
	void returnToTitle();

    GameState currentState{GameState::START};

private:
	GameState resumeState{GameState::IN_GAME};
};

#endif // GAMESTATE_HPP

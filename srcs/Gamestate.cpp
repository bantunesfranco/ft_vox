#include "Gamestate.hpp"

void GameStateController::toggleInventory() {
	if (currentState == GameState::IN_GAME)
		currentState = GameState::INVENTORY;
	else if (currentState == GameState::INVENTORY)
		currentState = GameState::IN_GAME;
	else
		return;
}

void GameStateController::togglePause() {
	if (currentState == GameState::INVENTORY) {
		currentState = GameState::IN_GAME;
	} else if (currentState == GameState::IN_GAME) {
		resumeState = GameState::IN_GAME;
		currentState = GameState::PAUSE;
	} else if (currentState == GameState::PAUSE) {
		currentState = resumeState;
	} else {
		return;
	}
}

void GameStateController::openMultiplayerMenu() {
	if (currentState == GameState::START)
		currentState = GameState::MULTIPLAYER;
}

void GameStateController::openMenu(const GameState menu) {
	if (isMenuState(currentState) && isMenuState(menu))
		currentState = menu;
}

void GameStateController::startGameplay() {
	if (!isMenuState(currentState))
		return;
	currentState = GameState::IN_GAME;
}

void GameStateController::returnToTitle() {
	currentState = GameState::START;
	resumeState = GameState::IN_GAME;
}

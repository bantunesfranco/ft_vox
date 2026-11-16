#ifndef GAME_MODE_HPP
#define GAME_MODE_HPP

enum class GameState {
	START,
	MULTIPLAYER,
	WORLD_SELECT,
	WORLD_CREATE,
	PAUSE,
	INVENTORY,
	IN_GAME,
};

// Title-screen menus: no world is shown or simulated behind them.
constexpr bool isMenuState(const GameState state) noexcept {
	return state == GameState::START || state == GameState::MULTIPLAYER
	    || state == GameState::WORLD_SELECT || state == GameState::WORLD_CREATE;
}

#endif

#include "Camera.hpp"
#include "Engine.hpp"

Camera::Camera() : 	pos{0.f, 0.f, 3.f}, dir {0.f, 0.f, -1.f}, up{0.f, 1.f, 0.f} {
	GLFWwindow *window = Engine::getInstance()->window;
	glfwGetCursorPos(window, &(mousePos[0]), &(mousePos[1]));
}

void	Camera::setCameraPosition(const vec3& newPos) {
	for (int i = 0; i < 3; i++)
		pos[i] = newPos[i];
};

void	Camera::setCameraDirection(const vec3& newDir) {
	for (int i = 0; i < 3; i++)
		dir[i] = newDir[i];
};
#include "Camera.hpp"

Camera::Camera(GLFWwindow *window) : pos{0.f, 0.f, 0.f}, dir {0.f, 0.f, 1.f}, up{0.f, 1.f, 0.f}, pitch(0), yaw(0), fov(90.0f) {
	glfwGetCursorPos(window, &(mousePos[0]), &(mousePos[1]));
}

void	Camera::setCameraPosition(const glm::vec3& newPos) {
	for (int i = 0; i < 3; i++)
		pos[i] = newPos[i];
};

void	Camera::setCameraDirection(const glm::vec3& newDir) {
	for (int i = 0; i < 3; i++)
		dir[i] = newDir[i];
};
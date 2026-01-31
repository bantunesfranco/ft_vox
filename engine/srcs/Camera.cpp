#include "Camera.hpp"

Camera::Camera(GLFWwindow *window) : pos{0.f, 25.f, 0.f}, dir {0.f, 0.f, 1.f}, up{0.f, 1.f, 0.f}, pitch(0), yaw(0), fov(80.0f)
{
	baseMoveSpeed = 10.0f;
	baseRotSpeed = 180.f;
	moveSpeed = baseMoveSpeed;
	rotSpeed = baseRotSpeed;
	lastPosition = pos;
	lastDirection = dir;
	rotationThreshold = 0.1f;
	movementThreshold = 0.5f;
	glfwGetCursorPos(window, &(mousePos[0]), &(mousePos[1]));
}

void	Camera::setCameraPosition(const glm::vec3& newPos) { pos = newPos; };

void	Camera::setCameraDirection(const glm::vec3& newDir) { dir = newDir; };

bool Camera::hasMovedOrRotated() const {
    const float rotationChange = glm::dot(glm::normalize(dir), glm::normalize(lastDirection));
    const float movementChange = glm::distance(pos, lastPosition);

    return rotationChange < (1.0f - rotationThreshold) || movementChange > movementThreshold;
}

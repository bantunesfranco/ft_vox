#include "Camera.hpp"

#include <cmath>

Camera::Camera(GLFWwindow*) :
	pos{0.f, 25.f, 0.f}, dir{0.f, 0.f, 1.f}, up{0.f, 1.f, 0.f},
	fov(70.0f), baseMoveSpeed(10.0f), baseRotSpeed(180.0f)
{
	moveSpeed = baseMoveSpeed;
	rotSpeed = baseRotSpeed;
}

glm::vec3 lookDirection(const float yawDegrees, const float pitchDegrees) {
	const float pitch = glm::radians(pitchDegrees);
	const float yaw = glm::radians(yawDegrees);
	return glm::normalize(glm::vec3{
		std::cos(yaw) * std::cos(pitch),
		std::sin(pitch),
		std::sin(yaw) * std::cos(pitch)
	});
}

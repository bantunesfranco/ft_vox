#include "Camera.hpp"

Camera::Camera(GLFWwindow*) :
	pos{0.f, 25.f, 0.f}, dir{0.f, 0.f, 1.f}, up{0.f, 1.f, 0.f},
	fov(90.0f), baseMoveSpeed(10.0f), baseRotSpeed(180.0f)
{
	moveSpeed = baseMoveSpeed;
	rotSpeed = baseRotSpeed;
}

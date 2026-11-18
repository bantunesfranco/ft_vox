#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "glm/glm.hpp"
#include "Renderer.hpp"

class Camera
{
	public:
		Camera(GLFWwindow *window);
		~Camera() = default;
		Camera(const Camera& camera) = delete;
		Camera& operator=(const Camera&) = delete;

		glm::vec3	pos;
		glm::vec3	dir;
		glm::vec3	up;
		float		pitch{};
		float		yaw{};
		float		fov;          // vertical, degrees (70 = Minecraft's default; 90 looked fish-eyed)
		glm::mat4	view{};
		glm::mat4	proj{};

		float baseMoveSpeed;
		float baseRotSpeed;
		float moveSpeed{};
		float rotSpeed{};
};

// Camera::dir for a yaw and pitch in degrees.
[[nodiscard]] glm::vec3 lookDirection(float yawDegrees, float pitchDegrees);

#endif

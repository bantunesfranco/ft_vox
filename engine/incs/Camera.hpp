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
		float		pitch;
		float		yaw;
		double		mousePos[2]{};
		float		fov;
		glm::mat4	view{};
		glm::mat4	proj{};

		float baseMoveSpeed;
		float baseRotSpeed;
		float moveSpeed;
		float rotSpeed;

		glm::vec3 lastPosition{};
		glm::vec3 lastDirection{};
		float rotationThreshold;
		float movementThreshold;

		void setCameraPosition(const glm::vec3& newPos);
		void setCameraDirection(const glm::vec3& newDir);
		[[nodiscard]] bool hasMovedOrRotated() const;
};

#endif
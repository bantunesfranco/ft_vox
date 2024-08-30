#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "glm/glm.hpp"
#include "Renderer.hpp"

#if defined(_WIN32) || defined(_WIN64)
    #ifndef M_PI
    #define M_PI 3.14159265358979323846
    #endif
#else
    #include <cmath>
#endif


#define DEG2RAD(deg) ((deg) * M_PI / 180.0)

class Camera
{
	public:
		Camera(GLFWwindow *window);
		~Camera() = default;
		Camera(const Camera& camera) = delete;
		Camera& operator=(const Camera&);

		glm::vec3	pos;
		glm::vec3	dir;
		glm::vec3	up;
		float		pitch;
		float		yaw;
		double		mousePos[2];

		void setCameraPosition(const glm::vec3& newPos);
		void setCameraDirection(const glm::vec3& newDir);
};

#endif
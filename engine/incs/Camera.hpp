#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "linmath.h"
#include "GLFW/glfw3.h"

class Camera
{
	public:
		Camera();
		~Camera() = default;
		Camera(const Camera& camera) = delete;
		Camera& operator=(const Camera&);

		vec3	pos;
		vec3	dir;
		vec3	up;
		double	mousePos[2];


		void setCameraPosition(const vec3& newPos);
		void setCameraDirection(const vec3& newDir);
};

#endif
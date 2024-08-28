#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "linmath.h"
#include "GLFW/glfw3.h"

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
		Camera();
		~Camera() = default;
		Camera(const Camera& camera) = delete;
		Camera& operator=(const Camera&);

		vec3	pos;
		vec3	dir;
		vec3	up;
		float	pitch;
		float	yaw;
		double	mousePos[2];

		void setCameraPosition(const vec3& newPos);
		void setCameraDirection(const vec3& newDir);
};

#endif
#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "linmath.h"

class Camera
{
	public:
		Camera();
		~Camera() = default;
		Camera(const Camera& camera) = delete;
		Camera& operator=(const Camera&);

		vec3 pos;
		vec3 front;
		vec3 up;

		void setCameraPosition(vec3 newPos);
		void setCameraDirection(vec3 newDir);
};

#endif
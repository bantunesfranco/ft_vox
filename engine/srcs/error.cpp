#include "Engine.hpp"

static const char*	vox_errors[VOX_ERRMAX] = {
	"No Errors",
	"File has invalid extension",
	"Failed to open the file",
	"PNG file is invalid or corrupted",
	"XPM42 file is invalid or corrupted",
	"The specified X or Y positions are out of bounds",
	"The specified Width or Height dimensions are out of bounds",
	"The provided image is invalid, might indicate mismanagement of images",
	"Failed to compile the vertex shader.",
	"Failed to compile the fragment shader.",
	"Failed to compile the shaders.",
	"Failed to load the texture",
	"Failed to allocate memory",
	"Failed to initialize GLAD",
	"Failed to initialize GLFW",
	"Failed to create window",
	"String is too big to be drawn",
};

const char* Engine::vox_strerror(vox_errno_t val)
{
	VOX_ASSERT(val >= 0, "Index must be positive");
	VOX_ASSERT(val < VOX_ERRMAX, "Index out of bounds");

	return (vox_errors[val]);
}

Engine::EngineException::EngineException(vox_errno_t err)
{
	vox_errno = err;
}

const char* Engine::EngineException::what() const noexcept
{
	return (vox_strerror(vox_errno));
}
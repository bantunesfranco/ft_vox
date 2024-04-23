#ifndef DEFINES_HPP
#define DEFINES_HPP

	typedef enum settings_e
	{
		VOX_STRETCH_IMAGE = 0,	// Image resize on window resize. Default: false
		VOX_FULLSCREEN,			// Start on fullscreen. Default: false
		VOX_MAXIMIZED,			// Start window maximized, overwrites fullscreen set true. Default: false
		VOX_DECORATED,			// Display window bar. Default: true
		VOX_HEADLESS,			// Run in headless mode, no window is created. Default: false
		VOX_SETTINGS_MAX,
	}	settings_t;

	typedef enum vox_errno_e
	{
		VOX_SUCCESS = 0,	// No Errors
		VOX_INVEXT,			// File has an invalid extension
		VOX_INVFILE,		// File was invalid / does not exist.
		VOX_INVPNG,			// Something is wrong with the given PNG file.
		VOX_INVXPM,			// Something is wrong with the given XPM file.
		VOX_INVPOS,			// The specified X/Y positions are out of bounds.
		VOX_INVDIM,			// The specified W/H dimensions are out of bounds.
		VOX_INVIMG,			// The provided image is invalid, might indicate mismanagement of images.
		VOX_VERTFAIL,		// Failed to compile the vertex shader.
		VOX_FRAGFAIL,		// Failed to compile the fragment shader.
		VOX_SHDRFAIL,		// Failed to compile the shaders.
		VOX_MEMFAIL,		// Dynamic memory allocation has failed.
		VOX_GLADFAIL,		// OpenGL loader has failed.
		VOX_GLFWFAIL,		// GLFW failed to initialize.
		VOX_WINFAIL,		// Failed to create a window.
		VOX_STRTOOBIG,		// The string is too big to be drawn.
		VOX_ERRMAX,			// Error count
	}	vox_errno_t;

#endif
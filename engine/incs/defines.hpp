#ifndef DEFINES_HPP
#define DEFINES_HPP

#include <glm/glm.hpp>

// Define a voxel as a 32-bit integer
typedef uint32_t Voxel;

typedef struct Vertex {
    glm::vec3 position;  // Vertex position
    glm::vec3 color;     // Vertex color
    glm::vec2 texCoords; // Texture coordinates
} Vertex;

typedef enum settings
{
	VOX_STRETCH_IMAGE = 0,	// Image resize on window resize. Default: false
	VOX_FULLSCREEN,			// Start on fullscreen. Default: false
	VOX_MAXIMIZED,			// Start window maximized, overwrites fullscreen if set true. Default: false
	VOX_DECORATED,			// Display window bar. Default: true
	VOX_HEADLESS,			// Run in headless mode, no window is created. Default: false
	VOX_RESIZE,				// Allow window resizing. Default: false
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


/**
 * A key action such as pressing or releasing a key.
 * 
 * @param RELEASE Execute when the key is being released.
 * @param PRESS Execute when the key is being pressed.
 * @param REPEAT Execute when the key is being held down.
 */
typedef enum action
{
	VOX_RELEASE = 0,
	VOX_PRESS	= 1,
	VOX_REPEAT	= 2,
}	action_t;

/**
 * Key modifiers, such as shift, control or alt.
 * These keys are flags meaning you can combine them to detect
 * key combinations such as CTRL + ALT so CTRL | ALT.
 * 
 * @param SHIFT The shift key.
 * @param CONTROL The control key.
 * @param ALT The alt key.
 * @param SUPERKEY The Superkey such as the Windows Key or Command.
 * @param CAPSLOCK The capslock key.
 * @param NUMLOCK The numlock key.
 */
typedef enum modifier_key
{
	VOX_SHIFT		= 0x0001,
	VOX_CONTROL		= 0x0002,
	VOX_ALT			= 0x0004,
	VOX_SUPERKEY	= 0x0008,
	VOX_CAPSLOCK	= 0x0010,
	VOX_NUMLOCK		= 0x0020,
}	modifier_key_t;

/**
 * The mouse button keycodes.
 * @param LEFT The left mouse button.
 * @param RIGHT The right mouse button.
 * @param MIDDLE The middle mouse button, aka the Scrollwheel.
 */
typedef enum mouse_key
{
	VOX_MOUSE_BUTTON_LEFT	= 0,
	VOX_MOUSE_BUTTON_RIGHT	= 1,
	VOX_MOUSE_BUTTON_MIDDLE	= 2,
}	mouse_key_t;

/**
 * Various mouse/cursor states.
 * @param NORMAL Simple visible default cursor.
 * @param HIDDEN The cursor is not rendered but still functions.
 * @param DISABLED The cursor is not rendered, nor is it functional.
 */
typedef enum mouse_mode
{
	VOX_MOUSE_NORMAL	= 0x00034001,
	VOX_MOUSE_HIDDEN	= 0x00034002,
	VOX_MOUSE_DISABLED	= 0x00034003,
}	mouse_mode_t;

/**
 * Various cursors that are standard.
 * @param ARROW The regular arrow cursor.
 * @param IBEAM The text input I-beam cursor shape.
 * @param CROSSHAIR The crosshair shape cursor.
 * @param HAND The hand shape cursor.
 * @param HRESIZE The horizontal resize arrow shape.
 * @param VRESIZE The vertical resize arrow shape.
 */
typedef enum cursor
{
	VOX_CURSOR_ARROW		= 0x00036001,
	VOX_CURSOR_IBEAM		= 0x00036002,
	VOX_CURSOR_CROSSHAIR	= 0x00036003,
	VOX_CURSOR_HAND			= 0x00036004,
	VOX_CURSOR_HRESIZE		= 0x00036005,
	VOX_CURSOR_VRESIZE		= 0x00036006,
}	cursor_t;

/**
 * All sorts of keyboard keycodes.
 * 
 * KP = Keypad.
 */
typedef enum keys
{
	VOX_KEY_SPACE			= 32,
	VOX_KEY_APOSTROPHE		= 39,
	VOX_KEY_COMMA			= 44,
	VOX_KEY_MINUS			= 45,
	VOX_KEY_PERIOD			= 46,
	VOX_KEY_SLASH			= 47,
	VOX_KEY_0				= 48,
	VOX_KEY_1				= 49,
	VOX_KEY_2				= 50,
	VOX_KEY_3				= 51,
	VOX_KEY_4				= 52,
	VOX_KEY_5				= 53,
	VOX_KEY_6				= 54,
	VOX_KEY_7				= 55,
	VOX_KEY_8				= 56,
	VOX_KEY_9				= 57,
	VOX_KEY_SEMICOLON		= 59,
	VOX_KEY_EQUAL			= 61,
	VOX_KEY_A				= 65,
	VOX_KEY_B				= 66,
	VOX_KEY_C				= 67,
	VOX_KEY_D				= 68,
	VOX_KEY_E				= 69,
	VOX_KEY_F				= 70,
	VOX_KEY_G				= 71,
	VOX_KEY_H				= 72,
	VOX_KEY_I				= 73,
	VOX_KEY_J				= 74,
	VOX_KEY_K				= 75,
	VOX_KEY_L				= 76,
	VOX_KEY_M				= 77,
	VOX_KEY_N				= 78,
	VOX_KEY_O				= 79,
	VOX_KEY_P				= 80,
	VOX_KEY_Q				= 81,
	VOX_KEY_R				= 82,
	VOX_KEY_S				= 83,
	VOX_KEY_T				= 84,
	VOX_KEY_U				= 85,
	VOX_KEY_V				= 86,
	VOX_KEY_W				= 87,
	VOX_KEY_X				= 88,
	VOX_KEY_Y				= 89,
	VOX_KEY_Z				= 90,
	VOX_KEY_LEFT_BRACKET	= 91,
	VOX_KEY_BACKSLASH		= 92,
	VOX_KEY_RIGHT_BRACKET	= 93,
	VOX_KEY_GRAVE_ACCENT	= 96,
	VOX_KEY_ESCAPE			= 256,
	VOX_KEY_ENTER			= 257,
	VOX_KEY_TAB				= 258,
	VOX_KEY_BACKSPACE		= 259,
	VOX_KEY_INSERT			= 260,
	VOX_KEY_DELETE			= 261,
	VOX_KEY_RIGHT			= 262,
	VOX_KEY_LEFT			= 263,
	VOX_KEY_DOWN			= 264,
	VOX_KEY_UP				= 265,
	VOX_KEY_PAGE_UP			= 266,
	VOX_KEY_PAGE_DOWN		= 267,
	VOX_KEY_HOME			= 268,
	VOX_KEY_END				= 269,
	VOX_KEY_CAPS_LOCK		= 280,
	VOX_KEY_SCROLL_LOCK		= 281,
	VOX_KEY_NUM_LOCK		= 282,
	VOX_KEY_PRINT_SCREEN	= 283,
	VOX_KEY_PAUSE			= 284,
	VOX_KEY_F1				= 290,
	VOX_KEY_F2				= 291,
	VOX_KEY_F3				= 292,
	VOX_KEY_F4				= 293,
	VOX_KEY_F5				= 294,
	VOX_KEY_F6				= 295,
	VOX_KEY_F7				= 296,
	VOX_KEY_F8				= 297,
	VOX_KEY_F9				= 298,
	VOX_KEY_F10				= 299,
	VOX_KEY_F11				= 300,
	VOX_KEY_F12				= 301,
	VOX_KEY_F13				= 302,
	VOX_KEY_F14				= 303,
	VOX_KEY_F15				= 304,
	VOX_KEY_F16				= 305,
	VOX_KEY_F17				= 306,
	VOX_KEY_F18				= 307,
	VOX_KEY_F19				= 308,
	VOX_KEY_F20				= 309,
	VOX_KEY_F21				= 310,
	VOX_KEY_F22				= 311,
	VOX_KEY_F23				= 312,
	VOX_KEY_F24				= 313,
	VOX_KEY_F25				= 314,
	VOX_KEY_KP_0			= 320,
	VOX_KEY_KP_1			= 321,
	VOX_KEY_KP_2			= 322,
	VOX_KEY_KP_3			= 323,
	VOX_KEY_KP_4			= 324,
	VOX_KEY_KP_5			= 325,
	VOX_KEY_KP_6			= 326,
	VOX_KEY_KP_7			= 327,
	VOX_KEY_KP_8			= 328,
	VOX_KEY_KP_9			= 329,
	VOX_KEY_KP_DECIMAL		= 330,
	VOX_KEY_KP_DIVIDE		= 331,
	VOX_KEY_KP_MULTIPLY		= 332,
	VOX_KEY_KP_SUBTRACT		= 333,
	VOX_KEY_KP_ADD			= 334,
	VOX_KEY_KP_ENTER		= 335,
	VOX_KEY_KP_EQUAL		= 336,
	VOX_KEY_LEFT_SHIFT		= 340,
	VOX_KEY_LEFT_CONTROL	= 341,
	VOX_KEY_LEFT_ALT		= 342,
	VOX_KEY_LEFT_SUPER		= 343,
	VOX_KEY_RIGHT_SHIFT		= 344,
	VOX_KEY_RIGHT_CONTROL	= 345,
	VOX_KEY_RIGHT_ALT		= 346,
	VOX_KEY_RIGHT_SUPER		= 347,
	VOX_KEY_MENU			= 348,
}	keys_t;

#endif
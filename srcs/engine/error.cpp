#include "Engine.hpp"

Engine::eng_error(const char* msg, const char* description)
{
	std::cerr << msg << description << std::endl;
}
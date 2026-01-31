#ifndef APP_HPP
#define APP_HPP

#include "Engine.hpp"
#include "BlockSystem.hpp"
#include "World.hpp"

struct HighlightedBlock {
	glm::vec3 highlightedBlockPos;
	GLuint highlightVAO;
	GLuint highlightVBO;
	GLuint highlightIBO;
	bool isHighlighted;
};

class App : public Engine
{
	public:
		App(int32_t width, int32_t height, const char *title, std::map<settings_t, bool>& settings);
		~App();
		App(const App&) = delete;
		App& operator=(const App&) = delete;

		void	run() override;
		void	terminate() override;
		void	destroyBlock();
		void	placeBlock();

		void	toggleFullscreen();
		void	toggleSpeedBoost();

		bool	showWireframe;
		bool	focused;
		GLuint textureArray;
		std::array<GLuint, 256> textureIndices;
		World world;
		BlockSystem blockSystem;
		std::unique_ptr<ThreadPool> threadPool;

		HighlightedBlock highlightedBlock;


	private:
		void	setCallbackFunctions() const;
		void	loadTextures();
		void	renderChunk(const Chunk& chunk, const WorldUBO& worldUbo, GLuint ubo, RenderType type) const;

		static void	queueVisibleChunksAO(World& world, const std::vector<std::pair<glm::ivec2, Chunk*>>& chunksToCalcAO, ThreadPool& threadPool);
		void setupHighlightCube();
		void cleanupHighlightCube();
		void updateBlockHighlight();
		void renderBlockHighlight();
		static void calcChunkAO(const glm::ivec2& coord, Chunk& chunk, const World& world);
		static void	uploadChunk(const Chunk& chunk, Chunk::ChunkRenderData& data);
};

void error_callback(int error, const char* description);
void cursor_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_bttn_callback(GLFWwindow* window, int button, int action, int mods);
void resize_callback(GLFWwindow* window, int width, int height);

void setupImGui(GLFWwindow* window);
// void renderImGui(const std::unique_ptr<Camera>& camera, bool showWireframe);
void renderImGui(const std::unique_ptr<Camera>& camera, bool showWireframe, float rgba[4], size_t chunkCount);



#endif
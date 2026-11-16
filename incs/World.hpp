#ifndef WORLD_HPP
#define WORLD_HPP

#include <atomic>
#include <memory>
#include <unordered_set>

#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ThreadPool.hpp"

class Frustum {
public:
	std::array<glm::vec4, 6> planes;

	void updateFrustum(const glm::mat4& proj, const glm::mat4& view) {
		// Compute clip matrix (Transpose it to safely extract rows using GLM indexing)
		glm::mat4 clip = glm::transpose(proj * view);

		// Now clip[0] is Row 1, clip[1] is Row 2, etc.
		planes[0] = clip[3] - clip[0]; // Right
		planes[1] = clip[3] + clip[0]; // Left
		planes[2] = clip[3] + clip[1]; // Bottom
		planes[3] = clip[3] - clip[1]; // Top
		planes[4] = clip[3] - clip[2]; // Far
		planes[5] = clip[3] + clip[2]; // Near

		// Normalize the planes
		for (auto& plane : planes) {
			plane /= glm::length(glm::vec3(plane));
		}
	}

	[[nodiscard]] bool isBoxInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const {
		const glm::vec3 center((chunkMin + chunkMax) * 0.5f);
		const glm::vec3 extents(chunkMax - center); // Constant vector for all uniform chunks!

		return std::ranges::all_of(planes, [&](const glm::vec4& plane) {
			const glm::vec3 normal(plane);

			// Project center onto the plane normal
			const float projectionCenter = glm::dot(normal, center) + plane.w;

			// Project extents box radius along the plane normal
			const float projectionRadius = glm::dot(glm::abs(normal), extents);

			// If the chunk center is further away than its radius, it is completely behind the plane
			return projectionCenter >= -projectionRadius;
		});
	}
};


class World {
public:
	static constexpr int CHUNK_RADIUS = 16;
	static constexpr int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;
	static constexpr int CHUNK_LOAD_RADIUS = CHUNK_RADIUS + 1;
	static constexpr int CHUNK_LOAD_DIAMETER = CHUNK_LOAD_RADIUS * 2 + 1;

	World() = default;

	void updateChunks(
		const glm::vec3& playerPos,
		ThreadPool& threadPool
	);

	void generateTerrain(
		ChunkColumn &column
	);

	// Meshing runs on `threadPool`: this applies finished meshes (a bounded
	// number per frame) and starts jobs for columns that need one, nearest /
	// in view first. Main thread only.
	void buildRenderStates(const glm::vec3& playerPosition, const glm::vec3& playerForward,
	                       ThreadPool& threadPool);
	std::vector<GpuMesh> pruneRenderStates();
	[[nodiscard]] bool getBlockAt(const glm::ivec3& worldPos, BlockId& block) const;
	bool setBlockAt(const glm::ivec3& worldPos, BlockId block);
	[[nodiscard]] bool getSurfaceHeight(int worldX, int worldZ, int& height) const;

	const BlockRegistry& getBlockRegistry() const noexcept;

	[[nodiscard]]
	const std::vector<ChunkRenderState>& getRenderStates() const noexcept;

	[[nodiscard]]
	std::vector<ChunkRenderState>& getRenderStates() noexcept;

	void saveChunk(const ChunkCoord& coord, const glm::ivec3& localPos, BlockId block) const;
	std::unordered_map<glm::ivec3, BlockId> loadChunk(const ChunkCoord& coord) const;

	// --- multiplayer hooks (main thread) ------------------------------------
	// Forgets every column. GPU meshes retire through pruneRenderStates(), and
	// generation jobs still running finish into the void.
	void clear();
	// Inserts (or replaces) a column built elsewhere (received from a server).
	void adoptColumn(std::unique_ptr<ChunkColumn> column);
	// Removes a column; its meshes retire through pruneRenderStates().
	void dropColumn(const ChunkCoord& coord);
	// When false, setBlockAt() no longer writes edits to saves/ (a client of a
	// server must not persist the server's world as its own).
	void setPersistence(bool enabled) noexcept { persistence = enabled; }

	Frustum frustum{};

private:
	glm::ivec2 playerChunk{
		std::numeric_limits<int>::max(),
		std::numeric_limits<int>::max()
	};

	// Columns are shared with mesh jobs, which hold their 3x3 neighbourhood
	// while they run. Writers copy a column that a job still holds before
	// changing it (see setBlockAt), so jobs read without any lock.
	using ColumnPtr = std::shared_ptr<ChunkColumn>;
	std::unordered_map<ChunkCoord, ColumnPtr> chunks;
	std::unordered_set<ChunkCoord> generatingChunks;

	// Main thread only.
	struct MeshStatus {
		std::uint64_t version  = 0;      // bumped on every change; older results are dropped
		bool          meshed   = false;  // render states match `version`
		bool          inFlight = false;  // a job is running
		std::uint32_t waited   = 0;      // frames spent waiting, for fair priority
	};
	std::unordered_map<ChunkCoord, MeshStatus> meshStatus;
	std::uint64_t meshVersionCounter = 0;          // versions are never reused
	std::size_t   meshJobsInFlight   = 0;

	// Worker -> main thread.
	struct MeshResult {
		ChunkCoord    coord;
		std::uint64_t version = 0;
		std::uint64_t epoch   = 0;
		std::vector<ChunkRenderState> states;
	};
	std::mutex              resultMutex;
	std::vector<MeshResult> meshResults;
	std::vector<MeshResult> readyResults;        // drained, waiting for their frame budget

	void applyMeshResults();
	void scheduleMeshJobs(const glm::vec3& playerPosition, const glm::vec3& playerForward,
	                      ThreadPool& threadPool);
	void replaceRenderStates(const ChunkCoord& coord, std::vector<ChunkRenderState> states);
	std::vector<ChunkRenderState> renderStates;
	std::vector<GpuMesh> pendingRetiredMeshes;

	mutable std::mutex chunk_mutex;
	// Bumped by clear(): a generation job started under another epoch drops its result.
	std::atomic<std::uint64_t> epoch{0};
	bool persistence = true;

	// Marks `coord` for remeshing; its current mesh stays visible until the
	// new one replaces it. chunk_mutex held.
	void invalidateMeshLocked(const ChunkCoord& coord);

	BlockRegistry blockRegistry{};
};

#endif

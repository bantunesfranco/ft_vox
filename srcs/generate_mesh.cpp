#include "Noise.hpp"
#include "voxel.hpp"
#include "vertex.hpp"
#include <Chunk.hpp>
#include <vector>

const int FACE_INDICES[24] = {
	0, 1, 2, 3, // Front face
	5, 4, 7, 6, // Back face
	3, 2, 6, 7, // Top face
	1, 0, 4, 5, // Bottom face
	4, 0, 3, 7, // Left face
	1, 5, 6, 2,  // Right face
};

const float VERTEX_POSITIONS[8][3] = {
	{0.0f, 0.0f, 0.0f},
	{1.0f, 0.0f, 0.0f},
	{1.0f, 1.0f, 0.0f},
	{0.0f, 1.0f, 0.0f},
	{0.0f, 0.0f, 1.0f},
	{1.0f, 0.0f, 1.0f},
	{1.0f, 1.0f, 1.0f},
	{0.0f, 1.0f, 1.0f}
};

bool isFaceVisible(const Chunk& chunk, int x, int y, int z, int face) {
	// Check adjacent voxel to determine if the face should be visible
	int dx = (face == 4) ? -1 : (face == 5) ? 1 : 0;
	int dy = (face == 1) ? -1 : (face == 3) ? 1 : 0;
	int dz = (face == 0) ? -1 : (face == 2) ? 1 : 0;

	int nx = x + dx;
	int ny = y + dy;
	int nz = z + dz;

	if (nx < 0 || nx >= CHUNK_WIDTH || ny < 0 || ny >= CHUNK_DEPTH || nz < 0 || nz >= CHUNK_HEIGHT) {
		// Face is on the edge of the chunk, so it's visible
		return true;
	}

	uint8_t isActive, r, g, b, blockType;
	unpackVoxelData(chunk.voxels[nx + CHUNK_WIDTH * (ny + CHUNK_DEPTH * nz)], isActive, r, g, b, blockType);
	return !isActive;
}

std::vector<Vertex> generateMesh(Chunk& chunk) {
	std::vector<Vertex> vertices;

	for (int x = 0; x < CHUNK_WIDTH; ++x) {
		for (int z = 0; z < CHUNK_DEPTH; ++z) {
			for (int y = 0; y < CHUNK_HEIGHT; ++y) {
				uint8_t isActive, r, g, b, blockType;
				unpackVoxelData(chunk.at(x, y, z), isActive, r, g, b, blockType);

				if (!isActive) continue;

				for (int face = 0; face < 6; ++face) {
					if (isFaceVisible(chunk, x, y, z, face)) {
						for (int i = 0; i < 4; ++i) {
							int idx = FACE_INDICES[face * 4 + i];
							Vertex vertex;
							vertex.pos[0] = x + VERTEX_POSITIONS[idx][0];
							vertex.pos[1] = y + VERTEX_POSITIONS[idx][1];
							vertex.pos[2] = z + VERTEX_POSITIONS[idx][2];
							vertex.col[0] = r / 255.0f;
							vertex.col[1] = g / 255.0f;
							vertex.col[2] = b / 255.0f;
							vertices.push_back(vertex);
						}
					}
				}
			}
		}
	}

	return vertices;
}

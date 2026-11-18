#include "SelfTest.hpp"

#include <algorithm>
#include <utility>

#include "BlockTextures.hpp"
#include "ChunkMesher.hpp"
#include "TestWorld.hpp"
#include "worldgen/TerrainGenerator.hpp"

namespace {

constexpr int FLOOR = 30;            // two sections of stone: section 0 is all underground

struct Meshed {
	MeshData opaque;
	MeshData transparent;
};

ChunkNeighborhood neighborhoodOf(const TestWorld& w, const ColumnLight* centreLight,
                                 const std::array<std::shared_ptr<const ColumnLight>, 9>* lights = nullptr) {
	ChunkNeighborhood neighborhood(&w.centre());
	for (int dz = -1; dz <= 1; ++dz)
		for (int dx = -1; dx <= 1; ++dx) {
			const auto i = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
			neighborhood.setNeighbor(dx, dz, w.columns[i].get());
			if (lights)
				neighborhood.setLight(dx, dz, (*lights)[i].get());
		}
	if (centreLight && !lights)
		neighborhood.setLight(0, 0, centreLight);
	return neighborhood;
}

// Meshes one section of the test world's centre column, with or without light.
Meshed mesh(const TestWorld& w, const int sectionY, const ColumnLight* centreLight,
            const std::array<std::shared_ptr<const ColumnLight>, 9>* lights = nullptr) {
	const ChunkNeighborhood neighborhood = neighborhoodOf(w, centreLight, lights);
	Meshed out;
	ChunkMesher::build(w.centre().section(sectionY), sectionY, neighborhood, w.registry, out.opaque, out.transparent);
	return out;
}

int normalOf(const PackedVertex& v) { return static_cast<int>((v.a >> 19u) & 0x07u); }
int skyOf(const PackedVertex& v) { return static_cast<int>(v.b & 0x0Fu); }
int blockOf(const PackedVertex& v) { return static_cast<int>((v.b >> 4u) & 0x0Fu); }
int aoOf(const PackedVertex& v) { return static_cast<int>((v.a >> 28u) & 0x03u); }
int cornerOf(const PackedVertex& v) { return static_cast<int>((v.a >> 30u) & 0x03u); }
int lightOf(const PackedVertex& v) { return (skyOf(v) << 4) | blockOf(v); }
std::array<int, 3> positionOf(const PackedVertex& v) {
	return {static_cast<int>(v.a & 0x1Fu), static_cast<int>((v.a >> 5u) & 0x1FFu), static_cast<int>((v.a >> 14u) & 0x1Fu)};
}

// The underside of the bottom layer: faces nobody sees, lit by "below the world" (dark).
bool worldBottom(const PackedVertex& v) {
	return normalOf(v) == static_cast<int>(NormalDir::NegativeY) && ((v.a >> 5u) & 0x1FFu) == 0u;
}

} // namespace

SELFTEST(Mesher, top_face_sky_lit) {
	const TestWorld w(FLOOR);
	const auto light = w.light();
	const Meshed m = mesh(w, FLOOR / 16, light.get());
	int tops = 0, wrong = 0;
	for (const PackedVertex& v : m.opaque.vertices)
		if (normalOf(v) == static_cast<int>(NormalDir::PositiveY)) {
			++tops;
			wrong += skyOf(v) == 15 ? 0 : 1;
		}
	CHECK(tops > 0);
	CHECK_EQ(wrong, 0);
}

SELFTEST(Mesher, sealed_pocket_dark) {
	TestWorld w(FLOOR);
	w.hollowBox(6, 4, 6, 8, 6, 8);                          // in section 0, under 24 blocks of stone
	const auto light = w.light();
	const Meshed m = mesh(w, 0, light.get());
	int bright = 0;
	for (const PackedVertex& v : m.opaque.vertices)
		bright += skyOf(v) + blockOf(v);
	CHECK(!m.opaque.vertices.empty());
	CHECK_EQ(bright, 0);
}

SELFTEST(Mesher, glowstone_lights_pocket) {
	TestWorld w(FLOOR);
	w.hollowBox(5, 3, 5, 9, 7, 9);
	w.set(7, 5, 7, toBlockId(BlockType::Glowstone));        // the middle of the pocket
	const auto light = w.light();
	const Meshed m = mesh(w, 0, light.get());
	int dimmest = 15, brightest = 0;
	for (const PackedVertex& v : m.opaque.vertices) {
		if (worldBottom(v))
			continue;
		dimmest = std::min(dimmest, blockOf(v));
		brightest = std::max(brightest, blockOf(v));
	}
	CHECK(dimmest >= 9);                                     // pocket walls: at most 6 steps away
	CHECK_EQ(brightest, 15);                                 // the glowstone's own faces
}

SELFTEST(Mesher, no_light_is_full_sky) {
	TestWorld w(FLOOR);
	w.hollowBox(6, 4, 6, 8, 6, 8);
	const Meshed m = mesh(w, 0, nullptr);                     // light not computed yet
	int dark = 0;
	for (const PackedVertex& v : m.opaque.vertices)
		dark += (skyOf(v) == 15 || worldBottom(v)) ? 0 : 1;
	CHECK(!m.opaque.vertices.empty());
	CHECK_EQ(dark, 0);
}

SELFTEST(Mesher, torch_is_a_stick) {
	TestWorld w(FLOOR);
	w.set(8, FLOOR + 1, 8, toBlockId(BlockType::Torch));      // standing on the floor
	const auto light = w.light();
	const Meshed m = mesh(w, (FLOOR + 1) / 16, light.get());
	const auto torchTexture = BlockTextures::index(BlockTextures::Texture::Torch);
	int vertices = 0, offGrid = 0, lit = 0;
	for (const PackedVertex& v : m.opaque.vertices) {
		if (((v.a >> 22u) & 0x3Fu) != torchTexture)
			continue;
		++vertices;
		const std::uint32_t sx = (v.b >> 8u) & 0x1Fu, sy = (v.b >> 13u) & 0x1Fu, sz = (v.b >> 18u) & 0x1Fu;
		offGrid += ((sx == 7 || sx == 9) && (sz == 7 || sz == 9) && (sy == 0 || sy == 10)) ? 0 : 1;
		lit += blockOf(v) >= 14 ? 1 : 0;
	}
	CHECK_EQ(vertices, 20);                                    // 4 sides + top
	CHECK_EQ(offGrid, 0);
	CHECK_EQ(lit, 20);                                         // it glows
}

SELFTEST(Mesher, cactus_sides_inset) {
	TestWorld w(FLOOR);
	// On a two-block pillar, so both cacti (y 33, 34) are in section 2.
	w.set(8, FLOOR + 1, 8, toBlockId(BlockType::Stone));
	w.set(8, FLOOR + 2, 8, toBlockId(BlockType::Stone));
	w.set(8, FLOOR + 3, 8, toBlockId(BlockType::Cactus));
	w.set(8, FLOOR + 4, 8, toBlockId(BlockType::Cactus));   // two high: no face between them
	const auto light = w.light();
	const Meshed m = mesh(w, (FLOOR + 3) / 16, light.get());
	const auto side = BlockTextures::index(BlockTextures::Texture::CactusSide);
	const auto top = BlockTextures::index(BlockTextures::Texture::CactusTop);
	int sides = 0, tops = 0, notInset = 0;
	for (const PackedVertex& v : m.opaque.vertices) {
		const std::uint32_t texture = (v.a >> 22u) & 0x3Fu;
		const int normal = normalOf(v);
		if (texture == side) {
			++sides;
			const bool xFace = normal == static_cast<int>(NormalDir::PositiveX) || normal == static_cast<int>(NormalDir::NegativeX);
			const std::uint32_t sub = xFace ? (v.b >> 8u) & 0x1Fu : (v.b >> 18u) & 0x1Fu;
			notInset += (sub == 1 || sub == 15) ? 0 : 1;
		} else if (texture == top && normal == static_cast<int>(NormalDir::PositiveY)) {
			++tops;
		}
	}
	CHECK_EQ(sides, 2 * 4 * 4);                                 // 4 sides x 2 blocks, 4 vertices each
	CHECK_EQ(notInset, 0);
	CHECK_EQ(tops, 4);                                          // only the top of the stack
}

// An uploaded mesh keeps no CPU copy: clear() would keep the capacity.
SELFTEST(Mesh, release_frees_capacity) {
	MeshData mesh;
	mesh.reserve(1000);
	mesh.vertices.resize(10);
	mesh.release();
	CHECK_EQ(mesh.vertices.capacity(), std::size_t{0});
	CHECK(mesh.empty());
}

// A quad whose AO diagonal flips is emitted v1 v2 v3 v0, so the shared
// pattern 0 1 2, 2 3 0 draws (v1 v2 v3), (v3 v0 v1): the old triangles.
SELFTEST(Mesher, flipped_quad_rotated) {
	TestWorld w(FLOOR);
	w.set(8, FLOOR + 1, 8, toBlockId(BlockType::Stone));       // shades corner 1 of the floor top at (7, 7)
	w.set(5, FLOOR + 1, 7, toBlockId(BlockType::Glowstone));   // two blocks away: light changes across that face
	const auto light = w.light();
	const ChunkNeighborhood neighborhood = neighborhoodOf(w, light.get());
	const Meshed m = mesh(w, FLOOR / 16, light.get());
	const auto& vertices = m.opaque.vertices;
	CHECK_EQ(vertices.size() % 4, std::size_t{0});

	// Every quad: rotated exactly when its AO diagonal flips, corners in order once undone.
	int rotated = 0, wrongOrder = 0, wrongFlip = 0;
	for (std::size_t q = 0; q + 4 <= vertices.size(); q += 4) {
		const bool isRotated = cornerOf(vertices[q]) == 1;
		std::array<PackedVertex, 4> old{};
		for (std::size_t slot = 0; slot < 4; ++slot)
			old[(slot + (isRotated ? 1 : 0)) % 4] = vertices[q + slot];
		for (int corner = 0; corner < 4; ++corner)
			wrongOrder += cornerOf(old[static_cast<std::size_t>(corner)]) == corner ? 0 : 1;
		const bool flips = aoOf(old[0]) + aoOf(old[2]) > aoOf(old[1]) + aoOf(old[3]);
		wrongFlip += flips == isRotated ? 0 : 1;
		rotated += isRotated ? 1 : 0;
	}
	CHECK_EQ(wrongOrder, 0);
	CHECK_EQ(wrongFlip, 0);
	CHECK(rotated > 0);

	// The floor top at (7, FLOOR, 7), section-local y 15, in emitQuad's
	// corner order for +Y: (-x +z) (+x +z) (+x -z) (-x -z).
	const int top = FLOOR + 1 - 16;
	const std::array<std::array<int, 3>, 4> corners = {{{7, top, 8}, {8, top, 8}, {8, top, 7}, {7, top, 7}}};
	const std::array<int, 4> ao = {3, 2, 3, 3};                // the stone is corner 1's diagonal
	const std::array<std::array<int, 2>, 4> uv = {{{-1, 1}, {1, 1}, {1, -1}, {-1, -1}}};
	// Smooth light as the mesher takes it: the open cells among the four above the corner.
	const auto addOpen = [&](const int x, const int z, int& sky, int& block, int& samples) {
		if (w.registry.isOpaque(neighborhood.getBlock(x, FLOOR + 1, z)))
			return false;
		const std::uint8_t packed = neighborhood.getLight(x, FLOOR + 1, z);
		sky += packed >> 4;
		block += packed & 0x0F;
		++samples;
		return true;
	};
	std::array<int, 4> expectedLight{};
	for (std::size_t corner = 0; corner < 4; ++corner) {
		int sky = 0, block = 0, samples = 0;
		addOpen(7, 7, sky, block, samples);
		const bool one = addOpen(7 + uv[corner][0], 7, sky, block, samples);
		const bool two = addOpen(7, 7 + uv[corner][1], sky, block, samples);
		if (one || two)
			addOpen(7 + uv[corner][0], 7 + uv[corner][1], sky, block, samples);
		expectedLight[corner] = ((sky / samples) << 4) | (block / samples);
	}
	CHECK(expectedLight[0] != expectedLight[2]);                // a gradient, so the order shows

	std::size_t found = vertices.size();
	for (std::size_t q = 0; q + 4 <= vertices.size(); q += 4) {
		if (normalOf(vertices[q]) != static_cast<int>(NormalDir::PositiveY))
			continue;
		std::array<std::array<int, 3>, 4> positions{};
		for (std::size_t slot = 0; slot < 4; ++slot)
			positions[slot] = positionOf(vertices[q + slot]);
		if (std::ranges::is_permutation(positions, corners))
			found = q;
	}
	CHECK(found < vertices.size());
	if (found >= vertices.size())
		return;
	// Emitted v1 v2 v3 v0: slot s holds old corner (s + 1) % 4, unchanged.
	for (std::size_t slot = 0; slot < 4; ++slot) {
		const PackedVertex& v = vertices[found + slot];
		const std::size_t corner = (slot + 1) % 4;
		CHECK_EQ(cornerOf(v), static_cast<int>(corner));
		CHECK(positionOf(v) == corners[corner]);
		CHECK_EQ(aoOf(v), ao[corner]);
		CHECK_EQ(lightOf(v), expectedLight[corner]);
	}
	// Through the pattern: slots (0 1 2) are old (1 2 3), slots (2 3 0) old (3 0 1).
	constexpr std::array<std::size_t, 6> pattern = {0, 1, 2, 2, 3, 0};
	constexpr std::array<int, 6> oldTriangles = {1, 2, 3, 3, 0, 1};
	for (std::size_t i = 0; i < pattern.size(); ++i)
		CHECK_EQ(cornerOf(vertices[found + pattern[i]]), oldTriangles[i]);
}

// 3D checkerboards in section 2: stone in air (6 faces a block, half the
// cells), and leaves among cacti, where every cell gives 6 quads: the bound.
SELFTEST(Mesher, worst_case_fits_shared_indices) {
	const auto checkerboard = [](const BlockId a, const BlockId b) {
		TestWorld w(FLOOR);
		for (int y = 32; y < 48; ++y)
			for (int z = 0; z < 16; ++z)
				for (int x = 0; x < 16; ++x)
					w.set(x, y, z, (x + y + z) % 2 == 0 ? a : b);
		const Meshed m = mesh(w, 2, nullptr);
		return m.opaque.quadCount() + m.transparent.quadCount();
	};
	const std::size_t stone = checkerboard(toBlockId(BlockType::Stone), AIR_BLOCK);
	CHECK_EQ(stone, std::size_t{6 * 16 * 16 * 16 / 2});
	CHECK(stone <= MAX_QUADS_PER_SECTION);
	const std::size_t leaves = checkerboard(toBlockId(BlockType::OakLeaves), toBlockId(BlockType::Cactus));
	CHECK_EQ(leaves, MAX_QUADS_PER_SECTION);
}

SELFTEST(Mesher, vertices_are_quads) {
	int bad = 0, meshes = 0;
	const auto check = [&](const Meshed& m) {
		for (const MeshData* data : {&m.opaque, &m.transparent}) {
			bad += data->vertices.size() % 4 == 0 ? 0 : 1;
			meshes += data->empty() ? 0 : 1;
		}
	};
	{
		TestWorld w(FLOOR);
		w.set(8, FLOOR + 1, 8, toBlockId(BlockType::Torch));
		check(mesh(w, (FLOOR + 1) / 16, nullptr));
	}
	{
		TestWorld w(FLOOR);
		w.set(8, FLOOR + 1, 8, toBlockId(BlockType::Cactus));
		w.set(8, FLOOR + 2, 8, toBlockId(BlockType::Cactus));
		check(mesh(w, (FLOOR + 1) / 16, nullptr));
	}
	CHECK_EQ(meshes, 2);
	{
		// Generated terrain around (0, 0), every section of the centre column.
		const worldgen::TerrainGenerator gen(1234);
		TestWorld w(-1);
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx) {
				auto column = std::make_unique<ChunkColumn>(ChunkCoord{dx, dz});
				gen.generate(*column);
				w.columns[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = std::move(column);
			}
		const auto light = w.light();
		for (int sectionY = 0; sectionY < ChunkColumn::SECTION_COUNT; ++sectionY)
			check(mesh(w, sectionY, light.get()));
	}
	CHECK(meshes > 4);
	CHECK_EQ(bad, 0);
}

namespace {

// FNV-1a 64 over every section's mesh of generated column `c` (seed 1234):
// per section the opaque then the transparent vertex count and vertices
// (a, b as little-endian u32). All 3x3 lights are computed, as in the game.
std::uint64_t columnMeshHash(const ChunkCoord c) {
	const worldgen::TerrainGenerator gen(1234);
	std::array<std::unique_ptr<ChunkColumn>, 25> columns;   // 5x5 around c: the 3x3 lights need it
	for (int dz = -2; dz <= 2; ++dz)
		for (int dx = -2; dx <= 2; ++dx) {
			auto column = std::make_unique<ChunkColumn>(ChunkCoord{c.x + dx, c.z + dz});
			gen.generate(*column);
			columns[static_cast<std::size_t>((dz + 2) * 5 + (dx + 2))] = std::move(column);
		}
	const auto at = [&](const int dx, const int dz) { return columns[static_cast<std::size_t>((dz + 2) * 5 + (dx + 2))].get(); };
	const BlockRegistry registry;
	std::array<std::shared_ptr<const ColumnLight>, 9> lights;
	ChunkNeighborhood neighborhood(at(0, 0));
	for (int dz = -1; dz <= 1; ++dz)
		for (int dx = -1; dx <= 1; ++dx) {
			ColumnWindow window{};
			for (int wz = -1; wz <= 1; ++wz)
				for (int wx = -1; wx <= 1; ++wx)
					window[static_cast<std::size_t>((wz + 1) * 3 + (wx + 1))] = at(dx + wx, dz + wz);
			const auto i = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
			lights[i] = computeLight(window, registry);
			neighborhood.setNeighbor(dx, dz, at(dx, dz));
			neighborhood.setLight(dx, dz, lights[i].get());
		}

	std::uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash](const std::uint32_t word) {
		for (int shift = 0; shift < 32; shift += 8)
			hash = (hash ^ ((word >> shift) & 0xFFu)) * 1099511628211ull;
	};
	MeshData opaque, transparent;
	for (int sectionY = 0; sectionY < ChunkColumn::SECTION_COUNT; ++sectionY) {
		ChunkMesher::build(at(0, 0)->section(sectionY), sectionY, neighborhood, registry, opaque, transparent);
		for (const MeshData* data : {&opaque, &transparent}) {
			mix(static_cast<std::uint32_t>(data->vertices.size()));
			for (const PackedVertex& v : data->vertices) {
				mix(v.a);
				mix(v.b);
			}
		}
	}
	return hash;
}

} // namespace

// The mesher's output for five generated columns, pinned: optimizations must
// not change a single vertex.
SELFTEST(Mesher, output_hash) {
	const std::array<std::pair<ChunkCoord, std::uint64_t>, 5> expected = {{
		{{0, 0}, 0x16e9ed869bb4e2a5ull},
		{{5, -3}, 0xec57a508ab6a257aull},
		{{-40, 17}, 0x9db630ca5e244cefull},
		{{200, 200}, 0x6d3d37ea6e45f01aull},
		{{-7, -900}, 0xce11d784f0e86514ull},
	}};
	for (const auto& [coord, hash] : expected) {
		CHECK_EQ(columnMeshHash(coord), hash);
	}
}

// The mesher's row copies read exactly what getBlock / getLight read, also
// for a missing column, a missing light and rows above and below the world.
SELFTEST(Mesher, row_copies_match_lookups) {
	TestWorld w(FLOOR);
	w.set(-1, FLOOR + 1, 3, toBlockId(BlockType::Glowstone));
	w.set(16, 5, 16, toBlockId(BlockType::Dirt));
	const auto light = w.light();
	ChunkNeighborhood neighborhood = neighborhoodOf(w, light.get());
	neighborhood.setNeighbor(1, -1, nullptr);          // a missing column reads as air
	int wrong = 0;
	std::array<BlockId, 18> blocks{};
	std::array<std::uint8_t, 18> lights{};
	for (const int y : {-1, 0, 5, FLOOR, FLOOR + 1, FLOOR + 2, ChunkColumn::HEIGHT - 1, ChunkColumn::HEIGHT})
		for (int z = -1; z <= 16; ++z) {
			neighborhood.copyBlocks(-1, 17, y, z, blocks.data());
			neighborhood.copyLight(-1, 17, y, z, lights.data());
			for (int x = -1; x <= 16; ++x) {
				const auto i = static_cast<std::size_t>(x + 1);
				wrong += blocks[i] == neighborhood.getBlock(x, y, z) ? 0 : 1;
				wrong += lights[i] == neighborhood.getLight(x, y, z) ? 0 : 1;
			}
		}
	CHECK_EQ(wrong, 0);
}

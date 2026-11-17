#include "SelfTest.hpp"
#include "BlockTextures.hpp"
#include "ChunkMesher.hpp"
#include "TestWorld.hpp"

namespace {

constexpr int FLOOR = 30;            // two sections of stone: section 0 is all underground

struct Meshed {
	MeshData opaque;
	MeshData transparent;
};

// Meshes one section of the test world's centre column, with or without light.
Meshed mesh(const TestWorld& w, const int sectionY, const ColumnLight* centreLight,
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
	Meshed out;
	ChunkMesher::build(w.centre().section(sectionY), sectionY, neighborhood, w.registry, out.opaque, out.transparent);
	return out;
}

int normalOf(const PackedVertex& v) { return static_cast<int>((v.a >> 19u) & 0x07u); }
int skyOf(const PackedVertex& v) { return static_cast<int>(v.b & 0x0Fu); }
int blockOf(const PackedVertex& v) { return static_cast<int>((v.b >> 4u) & 0x0Fu); }

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
	mesh.reserve(1000, 1000);
	mesh.vertices.resize(10);
	mesh.indices.resize(10);
	mesh.release();
	CHECK_EQ(mesh.vertices.capacity(), std::size_t{0});
	CHECK_EQ(mesh.indices.capacity(), std::size_t{0});
	CHECK(mesh.empty());
}

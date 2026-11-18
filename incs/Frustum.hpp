#ifndef FRUSTUM_HPP
#define FRUSTUM_HPP

#include <algorithm>
#include <array>

#include <glm/glm.hpp>

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

#endif

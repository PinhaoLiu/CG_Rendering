#pragma once

#include "simulation/SimulationScene.hpp"

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	struct UniformGridStats
	{
		glm::uvec3 dimensions{ 0u };
		float cellSize{ 0.0f };
		std::uint32_t cellCount{ 0u };
		std::uint32_t nonEmptyCellCount{ 0u };
		std::uint32_t maximumCellOccupancy{ 0u };
	};

	class UniformGrid
	{
	public:
		void configure(BoxBoundary const& boundary,
		               float minimum_cell_size,
		               std::size_t maximum_particle_count);
		void build(std::vector<glm::vec3> const& positions);

		template <typename Function>
		void forEachCandidate(glm::vec3 const& position, Function&& function) const
		{
			glm::uvec3 const center = cellCoordinates(position);
			std::uint32_t const min_x = center.x > 0u ? center.x - 1u : 0u;
			std::uint32_t const min_y = center.y > 0u ? center.y - 1u : 0u;
			std::uint32_t const min_z = center.z > 0u ? center.z - 1u : 0u;
			std::uint32_t const max_x = std::min(center.x + 1u, mDimensions.x - 1u);
			std::uint32_t const max_y = std::min(center.y + 1u, mDimensions.y - 1u);
			std::uint32_t const max_z = std::min(center.z + 1u, mDimensions.z - 1u);

			for (std::uint32_t z = min_z; z <= max_z; ++z) {
				for (std::uint32_t y = min_y; y <= max_y; ++y) {
					for (std::uint32_t x = min_x; x <= max_x; ++x) {
						std::uint32_t const cell = flatten(glm::uvec3(x, y, z));
						for (std::uint32_t offset = mCellOffsets[cell];
						     offset < mCellOffsets[cell + 1u];
						     ++offset)
							function(mParticleIndices[offset]);
					}
				}
			}
		}

		std::vector<std::uint32_t> collectNeighborIndices(
			std::size_t particle_index,
			std::vector<glm::vec3> const& positions,
			float radius,
			bool include_self = true) const;

		UniformGridStats const& stats() const noexcept;
		std::vector<std::uint32_t> const& cellCounts() const noexcept;
		std::vector<std::uint32_t> const& cellOffsets() const noexcept;
		std::vector<std::uint32_t> const& particleIndices() const noexcept;
		std::size_t activeParticleCount() const noexcept;

	private:
		glm::uvec3 cellCoordinates(glm::vec3 const& position) const noexcept;
		std::uint32_t flatten(glm::uvec3 const& coordinates) const noexcept;

		glm::vec3 mMinimum{ 0.0f };
		glm::vec3 mMaximum{ 0.0f };
		glm::uvec3 mDimensions{ 0u };
		float mCellSize{ 0.0f };
		std::size_t mMaximumParticleCount{ 0u };
		std::size_t mActiveParticleCount{ 0u };
		std::vector<std::uint32_t> mCellCounts;
		std::vector<std::uint32_t> mCellOffsets;
		std::vector<std::uint32_t> mCellWriteHeads;
		std::vector<std::uint32_t> mParticleIndices;
		UniformGridStats mStats;

		static constexpr std::uint32_t kMaximumCellsPerAxis = 128u;
	};
}

#include "simulation/UniformGrid.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sph
{
	void UniformGrid::configure(BoxBoundary const& boundary,
	                            float const minimum_cell_size,
	                            std::size_t const maximum_particle_count)
	{
		if (!(minimum_cell_size > 0.0f) || !std::isfinite(minimum_cell_size))
			throw std::invalid_argument("Uniform-grid cell size must be finite and positive.");
		if (maximum_particle_count == 0u ||
		    maximum_particle_count > std::numeric_limits<std::uint32_t>::max())
			throw std::invalid_argument("Uniform-grid particle capacity must fit uint32_t.");

		glm::vec3 const minimum = boundary.center - boundary.halfExtent;
		glm::vec3 const maximum = boundary.center + boundary.halfExtent;
		glm::vec3 const extent = maximum - minimum;
		if (!(extent.x > 0.0f) || !(extent.y > 0.0f) || !(extent.z > 0.0f) ||
		    !std::isfinite(extent.x) || !std::isfinite(extent.y) || !std::isfinite(extent.z))
			throw std::invalid_argument("Uniform-grid boundary extent must be finite and positive.");

		float const largest_extent = std::max(extent.x, std::max(extent.y, extent.z));
		float const cell_size = std::max(minimum_cell_size,
		                                 largest_extent / kMaximumCellsPerAxis);
		glm::uvec3 dimensions(
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.x / cell_size))),
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.y / cell_size))),
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.z / cell_size))));
		std::uint64_t const cell_count_64 = static_cast<std::uint64_t>(dimensions.x) *
		                                    dimensions.y * dimensions.z;
		if (cell_count_64 == 0u || cell_count_64 > std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("Uniform-grid cell count exceeds uint32_t.");

		std::uint32_t const cell_count = static_cast<std::uint32_t>(cell_count_64);
		bool const storage_changed = cell_count != mCellCounts.size() ||
		                             maximum_particle_count != mMaximumParticleCount;
		mMinimum = minimum;
		mMaximum = maximum;
		mDimensions = dimensions;
		mCellSize = cell_size;
		mMaximumParticleCount = maximum_particle_count;
		mStats.dimensions = dimensions;
		mStats.cellSize = cell_size;
		mStats.cellCount = cell_count;
		if (storage_changed) {
			mCellCounts.resize(cell_count);
			mCellOffsets.resize(static_cast<std::size_t>(cell_count) + 1u);
			mCellWriteHeads.resize(cell_count);
			mParticleIndices.resize(maximum_particle_count);
		}
	}

	void UniformGrid::build(std::vector<glm::vec3> const& positions)
	{
		if (mCellCounts.empty())
			throw std::logic_error("Uniform grid must be configured before build.");
		if (positions.size() > mMaximumParticleCount)
			throw std::length_error("Uniform-grid build exceeds particle capacity.");

		std::fill(mCellCounts.begin(), mCellCounts.end(), 0u);
		for (glm::vec3 const& position : positions)
			++mCellCounts[flatten(cellCoordinates(position))];

		mCellOffsets[0] = 0u;
		mStats.nonEmptyCellCount = 0u;
		mStats.maximumCellOccupancy = 0u;
		for (std::size_t cell = 0u; cell < mCellCounts.size(); ++cell) {
			mCellOffsets[cell + 1u] = mCellOffsets[cell] + mCellCounts[cell];
			mCellWriteHeads[cell] = mCellOffsets[cell];
			if (mCellCounts[cell] > 0u)
				++mStats.nonEmptyCellCount;
			mStats.maximumCellOccupancy =
				std::max(mStats.maximumCellOccupancy, mCellCounts[cell]);
		}

		for (std::size_t particle = 0u; particle < positions.size(); ++particle) {
			std::uint32_t const cell = flatten(cellCoordinates(positions[particle]));
			mParticleIndices[mCellWriteHeads[cell]++] = static_cast<std::uint32_t>(particle);
		}
		mActiveParticleCount = positions.size();
	}

	std::vector<std::uint32_t> UniformGrid::collectNeighborIndices(
		std::size_t const particle_index,
		std::vector<glm::vec3> const& positions,
		float const radius,
		bool const include_self) const
	{
		if (particle_index >= positions.size() || positions.size() != mActiveParticleCount)
			throw std::out_of_range("Uniform-grid neighbor query uses an invalid particle set.");
		if (!(radius > 0.0f) || radius > mCellSize)
			throw std::invalid_argument("Neighbor radius must be positive and no larger than cell size.");

		std::vector<std::uint32_t> neighbors;
		float const squared_radius = radius * radius;
		glm::vec3 const position = positions[particle_index];
		forEachCandidate(position, [&](std::uint32_t const candidate) {
			if (!include_self && candidate == particle_index)
				return;
			glm::vec3 const displacement = position - positions[candidate];
			if (glm::dot(displacement, displacement) < squared_radius)
				neighbors.push_back(candidate);
		});
		return neighbors;
	}

	UniformGridStats const& UniformGrid::stats() const noexcept
	{
		return mStats;
	}

	std::vector<std::uint32_t> const& UniformGrid::cellCounts() const noexcept
	{
		return mCellCounts;
	}

	std::vector<std::uint32_t> const& UniformGrid::cellOffsets() const noexcept
	{
		return mCellOffsets;
	}

	std::vector<std::uint32_t> const& UniformGrid::particleIndices() const noexcept
	{
		return mParticleIndices;
	}

	std::size_t UniformGrid::activeParticleCount() const noexcept
	{
		return mActiveParticleCount;
	}

	glm::uvec3 UniformGrid::cellCoordinates(glm::vec3 const& position) const noexcept
	{
		glm::vec3 const relative = (position - mMinimum) / mCellSize;
		auto const clamped_coordinate = [](float const value, std::uint32_t const dimension) {
			if (!(value > 0.0f))
				return 0u;
			if (value >= static_cast<float>(dimension))
				return dimension - 1u;
			return static_cast<std::uint32_t>(std::floor(value));
		};
		return glm::uvec3(clamped_coordinate(relative.x, mDimensions.x),
		                  clamped_coordinate(relative.y, mDimensions.y),
		                  clamped_coordinate(relative.z, mDimensions.z));
	}

	std::uint32_t UniformGrid::flatten(glm::uvec3 const& coordinates) const noexcept
	{
		return coordinates.x + mDimensions.x *
		       (coordinates.y + mDimensions.y * coordinates.z);
	}
}

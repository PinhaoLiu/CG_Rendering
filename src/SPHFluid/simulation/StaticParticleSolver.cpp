#include "simulation/StaticParticleSolver.hpp"

#include <cstddef>

namespace sph
{
	StaticParticleSolver::StaticParticleSolver(std::uint32_t const count_x,
	                                           std::uint32_t const count_y,
	                                           std::uint32_t const count_z,
	                                           float const spacing)
	{
		auto const particle_count = static_cast<std::size_t>(count_x) *
		                            static_cast<std::size_t>(count_y) *
		                            static_cast<std::size_t>(count_z);
		mInitialPositions.reserve(particle_count);

		float const width = static_cast<float>(count_x - 1u) * spacing;
		float const depth = static_cast<float>(count_z - 1u) * spacing;
		glm::vec3 const origin(-0.5f * width, 0.25f, -0.5f * depth);

		for (std::uint32_t y = 0u; y < count_y; ++y) {
			for (std::uint32_t z = 0u; z < count_z; ++z) {
				for (std::uint32_t x = 0u; x < count_x; ++x) {
					mInitialPositions.emplace_back(
						origin + spacing * glm::vec3(static_cast<float>(x),
						                             static_cast<float>(y),
						                             static_cast<float>(z)));
				}
			}
		}

		reset();
	}

	void StaticParticleSolver::reset()
	{
		mPositions = mInitialPositions;
	}

	std::size_t StaticParticleSolver::spawnParticles(std::vector<ParticleSpawn> const& particles)
	{
		(void)particles;
		return 0u;
	}

	void StaticParticleSolver::setBoundary(BoxBoundary const& boundary)
	{
		(void)boundary;
	}

	void StaticParticleSolver::step(float const fixed_delta_seconds)
	{
		// M0 intentionally keeps the particles static. M1 replaces this with
		// the correctness-first CPU WCSPH reference backend.
		(void)fixed_delta_seconds;
	}

	std::vector<glm::vec3> const& StaticParticleSolver::positions() const noexcept
	{
		return mPositions;
	}

	std::size_t StaticParticleSolver::capacity() const noexcept
	{
		return mInitialPositions.size();
	}

	char const* StaticParticleSolver::backendName() const noexcept
	{
		return "Static particle block (M0)";
	}
}

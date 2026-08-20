#pragma once

#include "simulation/ISphSolver.hpp"

#include <cstdint>
#include <vector>

namespace sph
{
	class StaticParticleSolver final : public ISphSolver
	{
	public:
		StaticParticleSolver(std::uint32_t count_x,
		                     std::uint32_t count_y,
		                     std::uint32_t count_z,
		                     float spacing);

		void reset() override;
		std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) override;
		void setBoundary(BoxBoundary const& boundary) override;
		void step(float fixed_delta_seconds) override;
		std::vector<glm::vec3> const& positions() const noexcept override;
		std::size_t capacity() const noexcept override;
		char const* backendName() const noexcept override;

	private:
		std::vector<glm::vec3> mInitialPositions;
		std::vector<glm::vec3> mPositions;
	};
}

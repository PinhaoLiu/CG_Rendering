#pragma once

#include "simulation/SimulationScene.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	class ISphSolver
	{
	public:
		virtual ~ISphSolver() = default;

		virtual void reset() = 0;
		virtual std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) = 0;
		virtual void setBoundary(BoxBoundary const& boundary) = 0;
		virtual void step(float fixed_delta_seconds) = 0;
		virtual std::vector<glm::vec3> const& positions() const noexcept = 0;
		virtual std::size_t particleCount() const noexcept
		{
			return positions().size();
		}
		virtual std::size_t capacity() const noexcept = 0;
		virtual char const* backendName() const noexcept = 0;
	};
}

#pragma once

#include "simulation/ParticleEmitter.hpp"
#include "simulation/SphTypes.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace sph
{
	class ParticleSoA
	{
	public:
		explicit ParticleSoA(std::size_t maximum_particle_count);

		void clear() noexcept;
		void append(ParticleSpawn const& particle, SphParameters const& parameters);
		std::size_t size() const noexcept;
		std::size_t capacity() const noexcept;
		std::size_t bytesPerParticle() const noexcept;
		std::size_t activeBytes() const noexcept;
		std::size_t reservedBytes() const noexcept;
		bool hasConsistentSizes() const noexcept;

		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> velocities;
		std::vector<glm::vec3> accelerations;
		std::vector<float> densities;
		std::vector<float> pressures;

	private:
		std::size_t mMaximumParticleCount;
	};
}

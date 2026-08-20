#include "simulation/ParticleSoA.hpp"

#include <stdexcept>

namespace sph
{
	ParticleSoA::ParticleSoA(std::size_t const maximum_particle_count) :
		mMaximumParticleCount(maximum_particle_count)
	{
		if (mMaximumParticleCount == 0u)
			throw std::invalid_argument("Particle capacity must be greater than zero.");
		positions.reserve(mMaximumParticleCount);
		velocities.reserve(mMaximumParticleCount);
		accelerations.reserve(mMaximumParticleCount);
		densities.reserve(mMaximumParticleCount);
		pressures.reserve(mMaximumParticleCount);
	}

	void ParticleSoA::clear() noexcept
	{
		positions.clear();
		velocities.clear();
		accelerations.clear();
		densities.clear();
		pressures.clear();
	}

	void ParticleSoA::append(ParticleSpawn const& particle,
	                         SphParameters const& parameters)
	{
		if (!hasConsistentSizes())
			throw std::logic_error("Particle SoA arrays have inconsistent sizes.");
		if (size() >= mMaximumParticleCount)
			throw std::length_error("Particle SoA capacity exceeded.");

		positions.push_back(particle.position);
		velocities.push_back(particle.velocity);
		accelerations.push_back(parameters.gravity);
		densities.push_back(parameters.restDensity);
		pressures.push_back(0.0f);
	}

	std::size_t ParticleSoA::size() const noexcept
	{
		return positions.size();
	}

	std::size_t ParticleSoA::capacity() const noexcept
	{
		return mMaximumParticleCount;
	}

	std::size_t ParticleSoA::bytesPerParticle() const noexcept
	{
		return 3u * sizeof(glm::vec3) + 2u * sizeof(float);
	}

	std::size_t ParticleSoA::activeBytes() const noexcept
	{
		return size() * bytesPerParticle();
	}

	std::size_t ParticleSoA::reservedBytes() const noexcept
	{
		return positions.capacity() * sizeof(glm::vec3) +
		       velocities.capacity() * sizeof(glm::vec3) +
		       accelerations.capacity() * sizeof(glm::vec3) +
		       densities.capacity() * sizeof(float) +
		       pressures.capacity() * sizeof(float);
	}

	bool ParticleSoA::hasConsistentSizes() const noexcept
	{
		return positions.size() == velocities.size() &&
		       positions.size() == accelerations.size() &&
		       positions.size() == densities.size() &&
		       positions.size() == pressures.size();
	}
}

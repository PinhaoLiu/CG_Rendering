#include "simulation/ParticleEmitter.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sph
{
	EmissionResult ParticleEmitter::generate(EmitterConfig const& config,
	                                         float const fixed_delta_seconds,
	                                         std::size_t const maximum_output,
	                                         std::vector<ParticleSpawn>& output)
	{
		output.clear();

		double const rate = std::max(0.0, std::min(1000000.0,
			static_cast<double>(config.particlesPerSecond)));
		mSpawnAccumulator += rate * static_cast<double>(fixed_delta_seconds);
		double const integral_count = std::floor(mSpawnAccumulator);
		mSpawnAccumulator -= integral_count;

		auto const size_limit = static_cast<double>(std::numeric_limits<std::size_t>::max());
		std::size_t const requested = integral_count >= size_limit
			? std::numeric_limits<std::size_t>::max()
			: static_cast<std::size_t>(integral_count);
		std::size_t const generated = std::min(requested, maximum_output);

		output.reserve(std::max(output.capacity(), generated));
		for (std::size_t index = 0u; index < generated; ++index)
			output.push_back(makeParticle(config, mSequence + index));

		mSequence += requested;
		return { requested, generated };
	}

	void ParticleEmitter::reset() noexcept
	{
		mSpawnAccumulator = 0.0;
		mSequence = 0u;
	}

	std::uint32_t ParticleEmitter::hash(std::uint32_t value) noexcept
	{
		value ^= value >> 16u;
		value *= 0x7feb352du;
		value ^= value >> 15u;
		value *= 0x846ca68bu;
		value ^= value >> 16u;
		return value;
	}

	float ParticleEmitter::unitFloat(std::uint32_t const value) noexcept
	{
		return static_cast<float>(value & 0x00ffffffu) / 16777216.0f;
	}

	ParticleSpawn ParticleEmitter::makeParticle(EmitterConfig const& config,
	                                           std::uint64_t const sequence) const
	{
		std::uint32_t const sequence32 = static_cast<std::uint32_t>(sequence) ^ config.seed;
		float const radial_sample = unitFloat(hash(sequence32 * 3u + 0u));
		float const azimuth_sample = unitFloat(hash(sequence32 * 3u + 1u));
		float const height_sample = unitFloat(hash(sequence32 * 3u + 2u));

		float const radial_distance = config.radius * std::cbrt(radial_sample);
		float const azimuth = glm::two_pi<float>() * azimuth_sample;
		float const height = 2.0f * height_sample - 1.0f;
		float const ring_radius = std::sqrt(std::max(0.0f, 1.0f - height * height));
		glm::vec3 const direction(ring_radius * std::cos(azimuth),
		                          height,
		                          ring_radius * std::sin(azimuth));

		return { config.position + radial_distance * direction, config.initialVelocity };
	}
}

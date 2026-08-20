#pragma once

#include "simulation/SimulationScene.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	struct EmissionResult
	{
		std::size_t requested{ 0u };
		std::size_t generated{ 0u };
	};

	class ParticleEmitter
	{
	public:
		EmissionResult generate(EmitterConfig const& config,
		                        float fixed_delta_seconds,
		                        std::size_t maximum_output,
		                        std::vector<ParticleSpawn>& output);
		void reset() noexcept;

	private:
		static std::uint32_t hash(std::uint32_t value) noexcept;
		static float unitFloat(std::uint32_t value) noexcept;
		ParticleSpawn makeParticle(EmitterConfig const& config, std::uint64_t sequence) const;

		double mSpawnAccumulator{ 0.0 };
		std::uint64_t mSequence{ 0u };
	};
}

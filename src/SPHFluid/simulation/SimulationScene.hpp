#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace sph
{
	struct ParticleSpawn
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 velocity{ 0.0f };
	};

	struct EmitterConfig
	{
		glm::vec3 position{ 0.0f, 2.6f, 0.0f };
		glm::vec3 initialVelocity{ 0.0f, -1.0f, 0.0f };
		float radius{ 0.18f };
		float particlesPerSecond{ 600.0f };
		std::uint32_t seed{ 1u };
		bool active{ false };
	};

	struct BoxBoundary
	{
		glm::vec3 center{ 0.0f, 1.5f, 0.0f };
		glm::vec3 halfExtent{ 2.5f, 1.5f, 1.75f };
		float restitution{ 0.2f };
		float friction{ 0.05f };
	};

	struct SimulationScene
	{
		EmitterConfig emitter;
		BoxBoundary boundary;
	};

	inline bool containsSphere(BoxBoundary const& boundary,
	                           glm::vec3 const& center,
	                           float const radius) noexcept
	{
		glm::vec3 const minimum = boundary.center - boundary.halfExtent;
		glm::vec3 const maximum = boundary.center + boundary.halfExtent;
		for (int axis = 0; axis < 3; ++axis) {
			if (center[axis] - radius < minimum[axis] ||
			    center[axis] + radius > maximum[axis])
				return false;
		}
		return true;
	}
}

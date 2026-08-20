#pragma once

#include <glm/vec3.hpp>

#include <vector>

namespace sph
{
	struct GpuParticleState
	{
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> velocities;
		std::vector<glm::vec3> accelerations;
		std::vector<float> densities;
		std::vector<float> pressures;
	};
}

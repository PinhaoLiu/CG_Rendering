#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>

namespace sph
{
	struct SphParameters
	{
		float smoothingRadius{ 0.16f };
		float particleRadius{ 0.04f };
		float restDensity{ 1000.0f };
		float particleMass{ 0.512f };
		float gasStiffness{ 50.0f };
		float viscosity{ 2.0f };
		glm::vec3 gravity{ 0.0f, -9.81f, 0.0f };
	};

	struct SphDiagnostics
	{
		float minimumDensity{ 0.0f };
		float maximumDensity{ 0.0f };
		float meanDensity{ 0.0f };
		float maximumSpeed{ 0.0f };
		double totalMass{ 0.0 };
		double kineticEnergy{ 0.0 };
		glm::vec3 centerOfMass{ 0.0f };
		std::uint64_t interactingDirectedPairs{ 0u };
		std::uint64_t completedSteps{ 0u };
		bool allFinite{ true };
	};

	struct SphStageTimings
	{
		double buildGridMilliseconds{ 0.0 };
		double densityPressureMilliseconds{ 0.0 };
		double forceMilliseconds{ 0.0 };
		double integrateMilliseconds{ 0.0 };
		double diagnosticsMilliseconds{ 0.0 };
		double wholeStepMilliseconds{ 0.0 };

		double totalMilliseconds() const noexcept
		{
			return buildGridMilliseconds + densityPressureMilliseconds +
			       forceMilliseconds + integrateMilliseconds + diagnosticsMilliseconds;
		}
	};

	struct ParticleStorageStats
	{
		std::size_t activeParticleCount{ 0u };
		std::size_t reservedParticleCapacity{ 0u };
		std::size_t particleArrayBytesPerParticle{ 0u };
		std::size_t activeParticleArrayBytes{ 0u };
		std::size_t reservedParticleArrayBytes{ 0u };
		bool duplicatesRenderPositions{ false };
	};
}

#pragma once

#include "simulation/ISphSolver.hpp"
#include "simulation/SphKernels.hpp"
#include "simulation/SphTypes.hpp"
#include "simulation/UniformGrid.hpp"

#include <cstddef>
#include <vector>

namespace sph
{
	class CpuGridSphSolver final : public ISphSolver
	{
	public:
		CpuGridSphSolver(std::size_t maximum_particle_count,
		                 SphParameters const& parameters = SphParameters{});

		void reset() override;
		std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) override;
		void setBoundary(BoxBoundary const& boundary) override;
		void step(float fixed_delta_seconds) override;
		std::vector<glm::vec3> const& positions() const noexcept override;
		std::size_t capacity() const noexcept override;
		char const* backendName() const noexcept override;

		float particleRadius() const noexcept;
		SphParameters const& parameters() const noexcept;
		void setParameters(SphParameters const& parameters);
		SphDiagnostics const& diagnostics() const noexcept;
		SphStageTimings const& stageTimings() const noexcept;
		UniformGridStats const& gridStats() const noexcept;
		ParticleStorageStats storageStats() const noexcept;

	private:
		struct Particle
		{
			glm::vec3 position{ 0.0f };
			glm::vec3 velocity{ 0.0f };
			glm::vec3 acceleration{ 0.0f };
			float density{ 0.0f };
			float pressure{ 0.0f };
		};

		static void validateParameters(SphParameters const& parameters);
		void computeDensityAndPressure();
		void computeAcceleration();
		void integrate(float fixed_delta_seconds);
		void collideWithBoundary(Particle& particle) const;
		void rebuildRenderPositionsAndDiagnostics();

		std::vector<Particle> mParticles;
		std::vector<glm::vec3> mRenderPositions;
		BoxBoundary mBoundary;
		std::size_t mMaximumParticleCount;
		SphParameters mParameters;
		SphKernels mKernels;
		UniformGrid mGrid;
		SphDiagnostics mDiagnostics;
		SphStageTimings mStageTimings;
	};
}

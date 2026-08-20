#pragma once

#include "simulation/FixedThreadPool.hpp"
#include "simulation/ISphSolver.hpp"
#include "simulation/ParticleSoA.hpp"
#include "simulation/SphKernels.hpp"
#include "simulation/SphTypes.hpp"
#include "simulation/UniformGrid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	class CpuParallelSphSolver final : public ISphSolver
	{
	public:
		CpuParallelSphSolver(std::size_t maximum_particle_count,
		                     SphParameters const& parameters = SphParameters{},
		                     std::size_t maximum_thread_count = 0u);

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
		bool particleArraysConsistent() const noexcept;
		std::size_t threadCount() const noexcept;
		std::size_t maximumThreadCount() const noexcept;
		void setThreadCount(std::size_t thread_count) noexcept;

	private:
		struct alignas(64) ThreadLocalStats
		{
			float minimumDensity{ 0.0f };
			float maximumDensity{ 0.0f };
			double densitySum{ 0.0 };
			std::uint64_t interactingDirectedPairs{ 0u };
		};

		static std::size_t resolveMaximumThreadCount(std::size_t requested) noexcept;
		static void validateParameters(SphParameters const& parameters);
		static void densityRangeThunk(void* context,
		                              std::size_t job_index,
		                              std::size_t begin,
		                              std::size_t end);
		static void forceRangeThunk(void* context,
		                            std::size_t job_index,
		                            std::size_t begin,
		                            std::size_t end);
		static void integrateRangeThunk(void* context,
		                                std::size_t job_index,
		                                std::size_t begin,
		                                std::size_t end);

		void computeDensityAndPressure();
		void computeDensityRange(std::size_t job_index,
		                         std::size_t begin,
		                         std::size_t end);
		void computeAcceleration();
		void computeAccelerationRange(std::size_t job_index,
		                              std::size_t begin,
		                              std::size_t end);
		void integrate(float fixed_delta_seconds);
		void integrateRange(std::size_t begin,
		                    std::size_t end,
		                    float fixed_delta_seconds);
		void collideWithBoundary(std::size_t particle_index);
		void rebuildDiagnostics();
		std::size_t activeJobCount() const noexcept;

		ParticleSoA mParticles;
		BoxBoundary mBoundary;
		std::size_t mMaximumParticleCount;
		SphParameters mParameters;
		SphKernels mKernels;
		UniformGrid mGrid;
		FixedThreadPool mThreadPool;
		std::vector<ThreadLocalStats> mThreadLocalStats;
		std::size_t mThreadCount;
		float mCurrentFixedDeltaSeconds{ 0.0f };
		SphDiagnostics mDiagnostics;
		SphStageTimings mStageTimings;
	};
}

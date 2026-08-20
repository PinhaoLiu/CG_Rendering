#pragma once

#include "simulation/ISphSolver.hpp"
#include "simulation/ParticleEmitter.hpp"

#include <cstdint>
#include <vector>

namespace sph
{
	class SimulationController
	{
	public:
		SimulationController(ISphSolver& solver, SimulationScene& scene);

		void advance(double frame_delta_seconds);
		void reset();
		void stepOnce();
		void setPaused(bool paused) noexcept;

		bool isPaused() const noexcept;
		float fixedDeltaSeconds() const noexcept;
		std::uint32_t maxSubsteps() const noexcept;
		std::uint64_t completedSteps() const noexcept;
		double simulatedSeconds() const noexcept;
		std::uint64_t droppedCatchUpFrames() const noexcept;
		std::uint64_t totalEmittedParticles() const noexcept;
		std::uint64_t droppedEmissionParticles() const noexcept;
		std::uint64_t blockedEmitterSteps() const noexcept;

	private:
		void executeStep();

		ISphSolver& mSolver;
		SimulationScene& mScene;
		ParticleEmitter mEmitter;
		std::vector<ParticleSpawn> mSpawnBatch;
		double mAccumulatorSeconds{ 0.0 };
		std::uint64_t mCompletedSteps{ 0u };
		std::uint64_t mDroppedCatchUpFrames{ 0u };
		std::uint64_t mTotalEmittedParticles{ 0u };
		std::uint64_t mDroppedEmissionParticles{ 0u };
		std::uint64_t mBlockedEmitterSteps{ 0u };
		bool mPaused{ false };

		static constexpr double kFixedDeltaSeconds = 1.0 / 120.0;
		static constexpr double kMaximumFrameDeltaSeconds = 0.25;
		static constexpr double kAccumulatorEpsilonSeconds = 1.0e-12;
		static constexpr std::uint32_t kMaximumSubsteps = 8u;
	};
}

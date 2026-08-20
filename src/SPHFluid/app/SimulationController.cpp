#include "app/SimulationController.hpp"

#include <algorithm>
#include <cmath>

namespace sph
{
	SimulationController::SimulationController(ISphSolver& solver, SimulationScene& scene) :
		mSolver(solver),
		mScene(scene)
	{
		mSpawnBatch.reserve(256u);
	}

	void SimulationController::advance(double const frame_delta_seconds)
	{
		if (mPaused)
			return;

		mAccumulatorSeconds += std::min(frame_delta_seconds, kMaximumFrameDeltaSeconds);
		std::uint32_t substep_count = 0u;
		while (mAccumulatorSeconds + kAccumulatorEpsilonSeconds >= kFixedDeltaSeconds &&
		       substep_count < kMaximumSubsteps) {
			executeStep();
			mAccumulatorSeconds -= kFixedDeltaSeconds;
			if (mAccumulatorSeconds < 0.0 &&
			    mAccumulatorSeconds > -kAccumulatorEpsilonSeconds)
				mAccumulatorSeconds = 0.0;
			++substep_count;
		}

		if (mAccumulatorSeconds >= kFixedDeltaSeconds) {
			mAccumulatorSeconds = std::fmod(mAccumulatorSeconds,
			                                static_cast<double>(kFixedDeltaSeconds));
			++mDroppedCatchUpFrames;
		}
	}

	void SimulationController::reset()
	{
		mSolver.reset();
		mEmitter.reset();
		mAccumulatorSeconds = 0.0;
		mCompletedSteps = 0u;
		mDroppedCatchUpFrames = 0u;
		mTotalEmittedParticles = 0u;
		mDroppedEmissionParticles = 0u;
		mBlockedEmitterSteps = 0u;
	}

	void SimulationController::stepOnce()
	{
		mPaused = true;
		mAccumulatorSeconds = 0.0;
		executeStep();
	}

	void SimulationController::setPaused(bool const paused) noexcept
	{
		if (mPaused != paused)
			mAccumulatorSeconds = 0.0;
		mPaused = paused;
	}

	bool SimulationController::isPaused() const noexcept
	{
		return mPaused;
	}

	float SimulationController::fixedDeltaSeconds() const noexcept
	{
		return static_cast<float>(kFixedDeltaSeconds);
	}

	std::uint32_t SimulationController::maxSubsteps() const noexcept
	{
		return kMaximumSubsteps;
	}

	std::uint64_t SimulationController::completedSteps() const noexcept
	{
		return mCompletedSteps;
	}

	double SimulationController::simulatedSeconds() const noexcept
	{
		return static_cast<double>(mCompletedSteps) * kFixedDeltaSeconds;
	}

	std::uint64_t SimulationController::droppedCatchUpFrames() const noexcept
	{
		return mDroppedCatchUpFrames;
	}

	std::uint64_t SimulationController::totalEmittedParticles() const noexcept
	{
		return mTotalEmittedParticles;
	}

	std::uint64_t SimulationController::droppedEmissionParticles() const noexcept
	{
		return mDroppedEmissionParticles;
	}

	std::uint64_t SimulationController::blockedEmitterSteps() const noexcept
	{
		return mBlockedEmitterSteps;
	}

	void SimulationController::executeStep()
	{
		mSolver.setBoundary(mScene.boundary);
		if (mScene.emitter.active) {
			if (containsSphere(mScene.boundary,
			                   mScene.emitter.position,
			                   mScene.emitter.radius)) {
				std::size_t const available = mSolver.capacity() - mSolver.particleCount();
				auto const result = mEmitter.generate(mScene.emitter,
				                                      static_cast<float>(kFixedDeltaSeconds),
				                                      available,
				                                      mSpawnBatch);
				std::size_t const accepted = mSolver.spawnParticles(mSpawnBatch);
				mTotalEmittedParticles += accepted;
				mDroppedEmissionParticles += result.requested - accepted;
			} else {
				++mBlockedEmitterSteps;
			}
		}
		mSolver.step(static_cast<float>(kFixedDeltaSeconds));
		++mCompletedSteps;
	}
}

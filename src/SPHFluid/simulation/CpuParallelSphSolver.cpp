#include "simulation/CpuParallelSphSolver.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace sph
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		double elapsedMilliseconds(Clock::time_point const begin,
		                           Clock::time_point const end)
		{
			return std::chrono::duration<double, std::milli>(end - begin).count();
		}
	}

	CpuParallelSphSolver::CpuParallelSphSolver(
		std::size_t const maximum_particle_count,
		SphParameters const& parameters,
		std::size_t const maximum_thread_count) :
		mParticles(maximum_particle_count),
		mMaximumParticleCount(maximum_particle_count),
		mParameters(parameters),
		mKernels(parameters.smoothingRadius),
		mThreadPool(resolveMaximumThreadCount(maximum_thread_count)),
		mThreadLocalStats(mThreadPool.maximumThreadCount()),
		mThreadCount(mThreadPool.maximumThreadCount())
	{
		validateParameters(mParameters);
		setBoundary(mBoundary);
	}

	void CpuParallelSphSolver::reset()
	{
		mParticles.clear();
		mGrid.build(mParticles.positions);
		mDiagnostics = {};
		mStageTimings = {};
	}

	std::size_t CpuParallelSphSolver::spawnParticles(
		std::vector<ParticleSpawn> const& particles)
	{
		std::size_t const available = mMaximumParticleCount - mParticles.size();
		std::size_t const accepted = std::min(available, particles.size());
		for (std::size_t index = 0u; index < accepted; ++index)
			mParticles.append(particles[index], mParameters);
		return accepted;
	}

	void CpuParallelSphSolver::setBoundary(BoxBoundary const& boundary)
	{
		mBoundary = boundary;
		float const minimum_extent = 2.0f * mParameters.particleRadius;
		for (int axis = 0; axis < 3; ++axis)
			mBoundary.halfExtent[axis] = std::max(minimum_extent, mBoundary.halfExtent[axis]);
		mBoundary.restitution = std::max(0.0f, std::min(1.0f, mBoundary.restitution));
		mBoundary.friction = std::max(0.0f, std::min(1.0f, mBoundary.friction));
		mGrid.configure(mBoundary, mParameters.smoothingRadius, mMaximumParticleCount);
	}

	void CpuParallelSphSolver::step(float const fixed_delta_seconds)
	{
		if (!(fixed_delta_seconds > 0.0f) || !std::isfinite(fixed_delta_seconds))
			throw std::invalid_argument("SPH fixed delta must be finite and positive.");
		if (!mParticles.hasConsistentSizes())
			throw std::logic_error("Particle SoA arrays have inconsistent sizes.");

		mStageTimings = {};
		auto const whole_step_begin = Clock::now();
		if (mParticles.size() == 0u) {
			++mDiagnostics.completedSteps;
			mStageTimings.wholeStepMilliseconds =
				elapsedMilliseconds(whole_step_begin, Clock::now());
			return;
		}

		auto begin = Clock::now();
		mGrid.build(mParticles.positions);
		auto end = Clock::now();
		mStageTimings.buildGridMilliseconds = elapsedMilliseconds(begin, end);

		begin = Clock::now();
		computeDensityAndPressure();
		end = Clock::now();
		mStageTimings.densityPressureMilliseconds = elapsedMilliseconds(begin, end);

		begin = Clock::now();
		computeAcceleration();
		end = Clock::now();
		mStageTimings.forceMilliseconds = elapsedMilliseconds(begin, end);

		begin = Clock::now();
		integrate(fixed_delta_seconds);
		end = Clock::now();
		mStageTimings.integrateMilliseconds = elapsedMilliseconds(begin, end);

		begin = Clock::now();
		rebuildDiagnostics();
		end = Clock::now();
		mStageTimings.diagnosticsMilliseconds = elapsedMilliseconds(begin, end);
		++mDiagnostics.completedSteps;
		mStageTimings.wholeStepMilliseconds =
			elapsedMilliseconds(whole_step_begin, Clock::now());
	}

	std::vector<glm::vec3> const& CpuParallelSphSolver::positions() const noexcept
	{
		return mParticles.positions;
	}

	std::size_t CpuParallelSphSolver::capacity() const noexcept
	{
		return mMaximumParticleCount;
	}

	char const* CpuParallelSphSolver::backendName() const noexcept
	{
		return "CPU WCSPH SoA Uniform Grid MT";
	}

	float CpuParallelSphSolver::particleRadius() const noexcept
	{
		return mParameters.particleRadius;
	}

	SphParameters const& CpuParallelSphSolver::parameters() const noexcept
	{
		return mParameters;
	}

	void CpuParallelSphSolver::setParameters(SphParameters const& parameters)
	{
		validateParameters(parameters);
		mParameters = parameters;
		mKernels = SphKernels(mParameters.smoothingRadius);
		setBoundary(mBoundary);
	}

	SphDiagnostics const& CpuParallelSphSolver::diagnostics() const noexcept
	{
		return mDiagnostics;
	}

	SphStageTimings const& CpuParallelSphSolver::stageTimings() const noexcept
	{
		return mStageTimings;
	}

	UniformGridStats const& CpuParallelSphSolver::gridStats() const noexcept
	{
		return mGrid.stats();
	}

	ParticleStorageStats CpuParallelSphSolver::storageStats() const noexcept
	{
		return { mParticles.size(),
		         mMaximumParticleCount,
		         mParticles.bytesPerParticle(),
		         mParticles.activeBytes(),
		         mParticles.reservedBytes(),
		         false };
	}

	bool CpuParallelSphSolver::particleArraysConsistent() const noexcept
	{
		return mParticles.hasConsistentSizes();
	}

	std::size_t CpuParallelSphSolver::threadCount() const noexcept
	{
		return mThreadCount;
	}

	std::size_t CpuParallelSphSolver::maximumThreadCount() const noexcept
	{
		return mThreadPool.maximumThreadCount();
	}

	void CpuParallelSphSolver::setThreadCount(std::size_t const thread_count) noexcept
	{
		mThreadCount = std::max<std::size_t>(
			1u,
			std::min(thread_count, mThreadPool.maximumThreadCount()));
	}

	std::size_t CpuParallelSphSolver::resolveMaximumThreadCount(
		std::size_t const requested) noexcept
	{
		if (requested > 0u)
			return requested;
		unsigned const hardware_threads = std::thread::hardware_concurrency();
		return std::max<std::size_t>(1u, hardware_threads);
	}

	void CpuParallelSphSolver::validateParameters(SphParameters const& parameters)
	{
		auto const positive_finite = [](float const value) {
			return value > 0.0f && std::isfinite(value);
		};
		if (!positive_finite(parameters.smoothingRadius) ||
		    !positive_finite(parameters.particleRadius) ||
		    parameters.smoothingRadius <= parameters.particleRadius ||
		    !positive_finite(parameters.restDensity) ||
		    !positive_finite(parameters.particleMass) ||
		    parameters.gasStiffness < 0.0f || !std::isfinite(parameters.gasStiffness) ||
		    parameters.viscosity < 0.0f || !std::isfinite(parameters.viscosity) ||
		    !std::isfinite(parameters.gravity.x) ||
		    !std::isfinite(parameters.gravity.y) ||
		    !std::isfinite(parameters.gravity.z))
			throw std::invalid_argument("Invalid WCSPH parameters.");
	}

	void CpuParallelSphSolver::densityRangeThunk(void* const context,
	                                             std::size_t const job_index,
	                                             std::size_t const begin,
	                                             std::size_t const end)
	{
		static_cast<CpuParallelSphSolver*>(context)->computeDensityRange(
			job_index, begin, end);
	}

	void CpuParallelSphSolver::forceRangeThunk(void* const context,
	                                           std::size_t const job_index,
	                                           std::size_t const begin,
	                                           std::size_t const end)
	{
		static_cast<CpuParallelSphSolver*>(context)->computeAccelerationRange(
			job_index, begin, end);
	}

	void CpuParallelSphSolver::integrateRangeThunk(void* const context,
	                                               std::size_t,
	                                               std::size_t const begin,
	                                               std::size_t const end)
	{
		CpuParallelSphSolver* const solver =
			static_cast<CpuParallelSphSolver*>(context);
		solver->integrateRange(begin, end, solver->mCurrentFixedDeltaSeconds);
	}

	void CpuParallelSphSolver::computeDensityAndPressure()
	{
		mThreadPool.parallelFor(mParticles.size(),
		                        mThreadCount,
		                        this,
		                        &CpuParallelSphSolver::densityRangeThunk);

		mDiagnostics.minimumDensity = std::numeric_limits<float>::max();
		mDiagnostics.maximumDensity = 0.0f;
		double density_sum = 0.0;
		for (std::size_t job = 0u; job < activeJobCount(); ++job) {
			ThreadLocalStats const& stats = mThreadLocalStats[job];
			mDiagnostics.minimumDensity =
				std::min(mDiagnostics.minimumDensity, stats.minimumDensity);
			mDiagnostics.maximumDensity =
				std::max(mDiagnostics.maximumDensity, stats.maximumDensity);
			density_sum += stats.densitySum;
		}
		mDiagnostics.meanDensity = static_cast<float>(density_sum / mParticles.size());
	}

	void CpuParallelSphSolver::computeDensityRange(std::size_t const job_index,
	                                               std::size_t const begin,
	                                               std::size_t const end)
	{
		ThreadLocalStats& stats = mThreadLocalStats[job_index];
		stats.minimumDensity = std::numeric_limits<float>::max();
		stats.maximumDensity = 0.0f;
		stats.densitySum = 0.0;
		for (std::size_t i = begin; i < end; ++i) {
			glm::vec3 const position_i = mParticles.positions[i];
			float density = 0.0f;
			mGrid.forEachCandidate(position_i, [&](std::uint32_t const j) {
				glm::vec3 const displacement = position_i - mParticles.positions[j];
				float const squared_distance = glm::dot(displacement, displacement);
				density += mParameters.particleMass * mKernels.poly6(squared_distance);
			});
			float const density_floor = 1.0e-3f * mParameters.restDensity;
			mParticles.densities[i] = std::max(density_floor, density);
			mParticles.pressures[i] = mParameters.gasStiffness *
				std::max(0.0f, mParticles.densities[i] - mParameters.restDensity);
			stats.minimumDensity = std::min(stats.minimumDensity, mParticles.densities[i]);
			stats.maximumDensity = std::max(stats.maximumDensity, mParticles.densities[i]);
			stats.densitySum += mParticles.densities[i];
		}
	}

	void CpuParallelSphSolver::computeAcceleration()
	{
		mThreadPool.parallelFor(mParticles.size(),
		                        mThreadCount,
		                        this,
		                        &CpuParallelSphSolver::forceRangeThunk);

		mDiagnostics.interactingDirectedPairs = 0u;
		for (std::size_t job = 0u; job < activeJobCount(); ++job)
			mDiagnostics.interactingDirectedPairs +=
				mThreadLocalStats[job].interactingDirectedPairs;
	}

	void CpuParallelSphSolver::computeAccelerationRange(
		std::size_t const job_index,
		std::size_t const begin,
		std::size_t const end)
	{
		std::uint64_t interacting_directed_pairs = 0u;
		for (std::size_t i = begin; i < end; ++i) {
			glm::vec3 const position_i = mParticles.positions[i];
			glm::vec3 const velocity_i = mParticles.velocities[i];
			float const density_i = mParticles.densities[i];
			float const pressure_i = mParticles.pressures[i];
			glm::vec3 acceleration = mParameters.gravity;
			mGrid.forEachCandidate(position_i, [&](std::uint32_t const j) {
				if (i == j)
					return;
				glm::vec3 const displacement = position_i - mParticles.positions[j];
				float const squared_distance = glm::dot(displacement, displacement);
				if (!(squared_distance < mKernels.squaredSmoothingRadius()) ||
				    squared_distance <= 1.0e-12f)
					return;

				float const distance = std::sqrt(squared_distance);
				glm::vec3 const pressure_gradient =
					mKernels.spikyGradient(displacement, distance);
				float const pressure_scale =
					-mParameters.particleMass * (pressure_i + mParticles.pressures[j]) /
					(2.0f * density_i * mParticles.densities[j]);
				acceleration += pressure_scale * pressure_gradient;

				float const viscosity_scale =
					mParameters.viscosity * mParameters.particleMass *
					mKernels.viscosityLaplacian(distance) /
					(density_i * mParticles.densities[j]);
				acceleration += viscosity_scale *
				                (mParticles.velocities[j] - velocity_i);
				++interacting_directed_pairs;
			});
			mParticles.accelerations[i] = acceleration;
		}
		mThreadLocalStats[job_index].interactingDirectedPairs =
			interacting_directed_pairs;
	}

	void CpuParallelSphSolver::integrate(float const fixed_delta_seconds)
	{
		mCurrentFixedDeltaSeconds = fixed_delta_seconds;
		mThreadPool.parallelFor(mParticles.size(),
		                        mThreadCount,
		                        this,
		                        &CpuParallelSphSolver::integrateRangeThunk);
	}

	void CpuParallelSphSolver::integrateRange(std::size_t const begin,
	                                          std::size_t const end,
	                                          float const fixed_delta_seconds)
	{
		for (std::size_t i = begin; i < end; ++i) {
			mParticles.velocities[i] += mParticles.accelerations[i] * fixed_delta_seconds;
			mParticles.positions[i] += mParticles.velocities[i] * fixed_delta_seconds;
			collideWithBoundary(i);
		}
	}

	void CpuParallelSphSolver::collideWithBoundary(std::size_t const particle_index)
	{
		glm::vec3& position = mParticles.positions[particle_index];
		glm::vec3& velocity = mParticles.velocities[particle_index];
		glm::vec3 const minimum = mBoundary.center - mBoundary.halfExtent +
		                          glm::vec3(mParameters.particleRadius);
		glm::vec3 const maximum = mBoundary.center + mBoundary.halfExtent -
		                          glm::vec3(mParameters.particleRadius);
		for (int axis = 0; axis < 3; ++axis) {
			bool collided = false;
			if (position[axis] < minimum[axis]) {
				position[axis] = minimum[axis];
				if (velocity[axis] < 0.0f)
					velocity[axis] *= -mBoundary.restitution;
				collided = true;
			} else if (position[axis] > maximum[axis]) {
				position[axis] = maximum[axis];
				if (velocity[axis] > 0.0f)
					velocity[axis] *= -mBoundary.restitution;
				collided = true;
			}
			if (collided) {
				float const tangential_scale = 1.0f - mBoundary.friction;
				for (int tangent_axis = 0; tangent_axis < 3; ++tangent_axis) {
					if (tangent_axis != axis)
						velocity[tangent_axis] *= tangential_scale;
				}
			}
		}
	}

	void CpuParallelSphSolver::rebuildDiagnostics()
	{
		mDiagnostics.maximumSpeed = 0.0f;
		mDiagnostics.totalMass = static_cast<double>(mParameters.particleMass) *
		                         static_cast<double>(mParticles.size());
		mDiagnostics.kineticEnergy = 0.0;
		mDiagnostics.centerOfMass = glm::vec3(0.0f);
		mDiagnostics.allFinite = true;

		for (std::size_t i = 0u; i < mParticles.size(); ++i) {
			glm::vec3 const position = mParticles.positions[i];
			glm::vec3 const velocity = mParticles.velocities[i];
			glm::vec3 const acceleration = mParticles.accelerations[i];
			float const squared_speed = glm::dot(velocity, velocity);
			mDiagnostics.maximumSpeed = std::max(
				mDiagnostics.maximumSpeed, std::sqrt(std::max(0.0f, squared_speed)));
			mDiagnostics.kineticEnergy +=
				0.5 * static_cast<double>(mParameters.particleMass) * squared_speed;
			mDiagnostics.centerOfMass += position;
			mDiagnostics.allFinite = mDiagnostics.allFinite &&
				std::isfinite(position.x) && std::isfinite(position.y) &&
				std::isfinite(position.z) && std::isfinite(velocity.x) &&
				std::isfinite(velocity.y) && std::isfinite(velocity.z) &&
				std::isfinite(mParticles.densities[i]) &&
				std::isfinite(mParticles.pressures[i]) &&
				std::isfinite(acceleration.x) && std::isfinite(acceleration.y) &&
				std::isfinite(acceleration.z);
		}
		mDiagnostics.centerOfMass /= static_cast<float>(mParticles.size());
		mDiagnostics.allFinite = mDiagnostics.allFinite &&
			std::isfinite(mDiagnostics.minimumDensity) &&
			std::isfinite(mDiagnostics.maximumDensity) &&
			std::isfinite(mDiagnostics.meanDensity) &&
			std::isfinite(mDiagnostics.maximumSpeed) &&
			std::isfinite(mDiagnostics.totalMass) &&
			std::isfinite(mDiagnostics.kineticEnergy);
	}

	std::size_t CpuParallelSphSolver::activeJobCount() const noexcept
	{
		return std::max<std::size_t>(
			1u,
			std::min({ mThreadCount,
			           mThreadPool.maximumThreadCount(),
			           mParticles.size() }));
	}
}

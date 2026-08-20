#include "simulation/CpuGridSphSolver.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

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

	CpuGridSphSolver::CpuGridSphSolver(std::size_t const maximum_particle_count,
	                                   SphParameters const& parameters) :
		mMaximumParticleCount(maximum_particle_count),
		mParameters(parameters),
		mKernels(parameters.smoothingRadius)
	{
		if (mMaximumParticleCount == 0u)
			throw std::invalid_argument("Particle capacity must be greater than zero.");
		validateParameters(mParameters);
		mParticles.reserve(mMaximumParticleCount);
		mRenderPositions.reserve(mMaximumParticleCount);
		setBoundary(mBoundary);
	}

	void CpuGridSphSolver::reset()
	{
		mParticles.clear();
		mRenderPositions.clear();
		mGrid.build(mRenderPositions);
		mDiagnostics = {};
		mStageTimings = {};
	}

	std::size_t CpuGridSphSolver::spawnParticles(
		std::vector<ParticleSpawn> const& particles)
	{
		std::size_t const available = mMaximumParticleCount - mParticles.size();
		std::size_t const accepted = std::min(available, particles.size());
		for (std::size_t index = 0u; index < accepted; ++index) {
			mParticles.push_back({ particles[index].position,
			                       particles[index].velocity,
			                       mParameters.gravity,
			                       mParameters.restDensity,
			                       0.0f });
			mRenderPositions.push_back(particles[index].position);
		}
		return accepted;
	}

	void CpuGridSphSolver::setBoundary(BoxBoundary const& boundary)
	{
		mBoundary = boundary;
		float const minimum_extent = 2.0f * mParameters.particleRadius;
		for (int axis = 0; axis < 3; ++axis)
			mBoundary.halfExtent[axis] = std::max(minimum_extent, mBoundary.halfExtent[axis]);
		mBoundary.restitution = std::max(0.0f, std::min(1.0f, mBoundary.restitution));
		mBoundary.friction = std::max(0.0f, std::min(1.0f, mBoundary.friction));
		mGrid.configure(mBoundary, mParameters.smoothingRadius, mMaximumParticleCount);
	}

	void CpuGridSphSolver::step(float const fixed_delta_seconds)
	{
		if (!(fixed_delta_seconds > 0.0f) || !std::isfinite(fixed_delta_seconds))
			throw std::invalid_argument("SPH fixed delta must be finite and positive.");
		mStageTimings = {};
		auto const whole_step_begin = Clock::now();
		if (mParticles.empty()) {
			++mDiagnostics.completedSteps;
			mStageTimings.wholeStepMilliseconds =
				elapsedMilliseconds(whole_step_begin, Clock::now());
			return;
		}

		auto begin = Clock::now();
		mGrid.build(mRenderPositions);
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
		rebuildRenderPositionsAndDiagnostics();
		end = Clock::now();
		mStageTimings.diagnosticsMilliseconds = elapsedMilliseconds(begin, end);
		++mDiagnostics.completedSteps;
		mStageTimings.wholeStepMilliseconds =
			elapsedMilliseconds(whole_step_begin, Clock::now());
	}

	std::vector<glm::vec3> const& CpuGridSphSolver::positions() const noexcept
	{
		return mRenderPositions;
	}

	std::size_t CpuGridSphSolver::capacity() const noexcept
	{
		return mMaximumParticleCount;
	}

	char const* CpuGridSphSolver::backendName() const noexcept
	{
		return "CPU WCSPH AoS Uniform Grid";
	}

	float CpuGridSphSolver::particleRadius() const noexcept
	{
		return mParameters.particleRadius;
	}

	SphParameters const& CpuGridSphSolver::parameters() const noexcept
	{
		return mParameters;
	}

	void CpuGridSphSolver::setParameters(SphParameters const& parameters)
	{
		validateParameters(parameters);
		mParameters = parameters;
		mKernels = SphKernels(mParameters.smoothingRadius);
		setBoundary(mBoundary);
	}

	SphDiagnostics const& CpuGridSphSolver::diagnostics() const noexcept
	{
		return mDiagnostics;
	}

	SphStageTimings const& CpuGridSphSolver::stageTimings() const noexcept
	{
		return mStageTimings;
	}

	UniformGridStats const& CpuGridSphSolver::gridStats() const noexcept
	{
		return mGrid.stats();
	}

	ParticleStorageStats CpuGridSphSolver::storageStats() const noexcept
	{
		std::size_t const bytes_per_particle = sizeof(Particle) + sizeof(glm::vec3);
		return { mParticles.size(),
		         mMaximumParticleCount,
		         bytes_per_particle,
		         mParticles.size() * sizeof(Particle) +
		             mRenderPositions.size() * sizeof(glm::vec3),
		         mParticles.capacity() * sizeof(Particle) +
		             mRenderPositions.capacity() * sizeof(glm::vec3),
		         true };
	}

	void CpuGridSphSolver::validateParameters(SphParameters const& parameters)
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

	void CpuGridSphSolver::computeDensityAndPressure()
	{
		mDiagnostics.minimumDensity = std::numeric_limits<float>::max();
		mDiagnostics.maximumDensity = 0.0f;
		double density_sum = 0.0;

		for (std::size_t i = 0u; i < mParticles.size(); ++i) {
			Particle& particle_i = mParticles[i];
			float density = 0.0f;
			mGrid.forEachCandidate(particle_i.position, [&](std::uint32_t const j) {
				glm::vec3 const displacement = particle_i.position - mParticles[j].position;
				float const squared_distance = glm::dot(displacement, displacement);
				density += mParameters.particleMass * mKernels.poly6(squared_distance);
			});
			float const density_floor = 1.0e-3f * mParameters.restDensity;
			particle_i.density = std::max(density_floor, density);
			particle_i.pressure = mParameters.gasStiffness *
			                      std::max(0.0f, particle_i.density - mParameters.restDensity);
			mDiagnostics.minimumDensity = std::min(mDiagnostics.minimumDensity, particle_i.density);
			mDiagnostics.maximumDensity = std::max(mDiagnostics.maximumDensity, particle_i.density);
			density_sum += particle_i.density;
		}
		mDiagnostics.meanDensity = static_cast<float>(density_sum / mParticles.size());
	}

	void CpuGridSphSolver::computeAcceleration()
	{
		mDiagnostics.interactingDirectedPairs = 0u;
		for (std::size_t i = 0u; i < mParticles.size(); ++i) {
			Particle& particle_i = mParticles[i];
			glm::vec3 acceleration = mParameters.gravity;
			mGrid.forEachCandidate(particle_i.position, [&](std::uint32_t const j) {
				if (i == j)
					return;
				Particle const& particle_j = mParticles[j];
				glm::vec3 const displacement = particle_i.position - particle_j.position;
				float const squared_distance = glm::dot(displacement, displacement);
				if (!(squared_distance < mKernels.squaredSmoothingRadius()) ||
				    squared_distance <= 1.0e-12f)
					return;

				float const distance = std::sqrt(squared_distance);
				glm::vec3 const pressure_gradient =
					mKernels.spikyGradient(displacement, distance);
				float const pressure_scale =
					-mParameters.particleMass * (particle_i.pressure + particle_j.pressure) /
					(2.0f * particle_i.density * particle_j.density);
				acceleration += pressure_scale * pressure_gradient;

				float const viscosity_scale =
					mParameters.viscosity * mParameters.particleMass *
					mKernels.viscosityLaplacian(distance) /
					(particle_i.density * particle_j.density);
				acceleration += viscosity_scale * (particle_j.velocity - particle_i.velocity);
				++mDiagnostics.interactingDirectedPairs;
			});
			particle_i.acceleration = acceleration;
		}
	}

	void CpuGridSphSolver::integrate(float const fixed_delta_seconds)
	{
		for (Particle& particle : mParticles) {
			particle.velocity += particle.acceleration * fixed_delta_seconds;
			particle.position += particle.velocity * fixed_delta_seconds;
			collideWithBoundary(particle);
		}
	}

	void CpuGridSphSolver::collideWithBoundary(Particle& particle) const
	{
		glm::vec3 const minimum = mBoundary.center - mBoundary.halfExtent +
		                          glm::vec3(mParameters.particleRadius);
		glm::vec3 const maximum = mBoundary.center + mBoundary.halfExtent -
		                          glm::vec3(mParameters.particleRadius);
		for (int axis = 0; axis < 3; ++axis) {
			bool collided = false;
			if (particle.position[axis] < minimum[axis]) {
				particle.position[axis] = minimum[axis];
				if (particle.velocity[axis] < 0.0f)
					particle.velocity[axis] *= -mBoundary.restitution;
				collided = true;
			} else if (particle.position[axis] > maximum[axis]) {
				particle.position[axis] = maximum[axis];
				if (particle.velocity[axis] > 0.0f)
					particle.velocity[axis] *= -mBoundary.restitution;
				collided = true;
			}
			if (collided) {
				float const tangential_scale = 1.0f - mBoundary.friction;
				for (int tangent_axis = 0; tangent_axis < 3; ++tangent_axis) {
					if (tangent_axis != axis)
						particle.velocity[tangent_axis] *= tangential_scale;
				}
			}
		}
	}

	void CpuGridSphSolver::rebuildRenderPositionsAndDiagnostics()
	{
		mRenderPositions.resize(mParticles.size());
		mDiagnostics.maximumSpeed = 0.0f;
		mDiagnostics.totalMass = static_cast<double>(mParameters.particleMass) *
		                         static_cast<double>(mParticles.size());
		mDiagnostics.kineticEnergy = 0.0;
		mDiagnostics.centerOfMass = glm::vec3(0.0f);
		mDiagnostics.allFinite = true;

		for (std::size_t index = 0u; index < mParticles.size(); ++index) {
			Particle const& particle = mParticles[index];
			mRenderPositions[index] = particle.position;
			float const squared_speed = glm::dot(particle.velocity, particle.velocity);
			mDiagnostics.maximumSpeed = std::max(mDiagnostics.maximumSpeed,
			                                     std::sqrt(std::max(0.0f, squared_speed)));
			mDiagnostics.kineticEnergy +=
				0.5 * static_cast<double>(mParameters.particleMass) * squared_speed;
			mDiagnostics.centerOfMass += particle.position;
			mDiagnostics.allFinite = mDiagnostics.allFinite &&
				std::isfinite(particle.position.x) &&
				std::isfinite(particle.position.y) &&
				std::isfinite(particle.position.z) &&
				std::isfinite(particle.velocity.x) &&
				std::isfinite(particle.velocity.y) &&
				std::isfinite(particle.velocity.z) &&
				std::isfinite(particle.density) &&
				std::isfinite(particle.pressure) &&
				std::isfinite(particle.acceleration.x) &&
				std::isfinite(particle.acceleration.y) &&
				std::isfinite(particle.acceleration.z);
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
}

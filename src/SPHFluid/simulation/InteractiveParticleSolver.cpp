#include "simulation/InteractiveParticleSolver.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sph
{
	InteractiveParticleSolver::InteractiveParticleSolver(
		std::size_t const maximum_particle_count,
		float const particle_radius) :
		mMaximumParticleCount(maximum_particle_count),
		mParticleRadius(particle_radius)
	{
		if (mMaximumParticleCount == 0u)
			throw std::invalid_argument("Particle capacity must be greater than zero.");
		if (!(mParticleRadius > 0.0f))
			throw std::invalid_argument("Particle radius must be greater than zero.");

		mParticles.reserve(mMaximumParticleCount);
		mRenderPositions.reserve(mMaximumParticleCount);
		setBoundary(mBoundary);
	}

	void InteractiveParticleSolver::reset()
	{
		mParticles.clear();
		mRenderPositions.clear();
	}

	std::size_t InteractiveParticleSolver::spawnParticles(
		std::vector<ParticleSpawn> const& particles)
	{
		std::size_t const available = mMaximumParticleCount - mParticles.size();
		std::size_t const accepted = std::min(available, particles.size());
		for (std::size_t index = 0u; index < accepted; ++index) {
			mParticles.push_back({ particles[index].position, particles[index].velocity });
			mRenderPositions.push_back(particles[index].position);
		}
		return accepted;
	}

	void InteractiveParticleSolver::setBoundary(BoxBoundary const& boundary)
	{
		mBoundary = boundary;
		float const minimum_extent = 2.0f * mParticleRadius;
		for (int axis = 0; axis < 3; ++axis)
			mBoundary.halfExtent[axis] = std::max(minimum_extent, mBoundary.halfExtent[axis]);
		mBoundary.restitution = std::max(0.0f, std::min(1.0f, mBoundary.restitution));
		mBoundary.friction = std::max(0.0f, std::min(1.0f, mBoundary.friction));
	}

	void InteractiveParticleSolver::step(float const fixed_delta_seconds)
	{
		for (auto& particle : mParticles) {
			particle.velocity += mGravity * fixed_delta_seconds;
			particle.position += particle.velocity * fixed_delta_seconds;
			collideWithBoundary(particle);
		}
		rebuildRenderPositions();
	}

	std::vector<glm::vec3> const& InteractiveParticleSolver::positions() const noexcept
	{
		return mRenderPositions;
	}

	std::size_t InteractiveParticleSolver::capacity() const noexcept
	{
		return mMaximumParticleCount;
	}

	char const* InteractiveParticleSolver::backendName() const noexcept
	{
		return "CPU interactive particles (pre-SPH)";
	}

	float InteractiveParticleSolver::particleRadius() const noexcept
	{
		return mParticleRadius;
	}

	void InteractiveParticleSolver::collideWithBoundary(Particle& particle) const
	{
		glm::vec3 const minimum = mBoundary.center - mBoundary.halfExtent +
		                          glm::vec3(mParticleRadius);
		glm::vec3 const maximum = mBoundary.center + mBoundary.halfExtent -
		                          glm::vec3(mParticleRadius);

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

	void InteractiveParticleSolver::rebuildRenderPositions()
	{
		mRenderPositions.resize(mParticles.size());
		for (std::size_t index = 0u; index < mParticles.size(); ++index)
			mRenderPositions[index] = mParticles[index].position;
	}
}

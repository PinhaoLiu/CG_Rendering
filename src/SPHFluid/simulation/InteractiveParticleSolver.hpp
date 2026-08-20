#pragma once

#include "simulation/ISphSolver.hpp"

#include <cstddef>
#include <vector>

namespace sph
{
	class InteractiveParticleSolver final : public ISphSolver
	{
	public:
		InteractiveParticleSolver(std::size_t maximum_particle_count, float particle_radius);

		void reset() override;
		std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) override;
		void setBoundary(BoxBoundary const& boundary) override;
		void step(float fixed_delta_seconds) override;
		std::vector<glm::vec3> const& positions() const noexcept override;
		std::size_t capacity() const noexcept override;
		char const* backendName() const noexcept override;

		float particleRadius() const noexcept;

	private:
		struct Particle
		{
			glm::vec3 position{ 0.0f };
			glm::vec3 velocity{ 0.0f };
		};

		void collideWithBoundary(Particle& particle) const;
		void rebuildRenderPositions();

		std::vector<Particle> mParticles;
		std::vector<glm::vec3> mRenderPositions;
		BoxBoundary mBoundary;
		std::size_t mMaximumParticleCount;
		float mParticleRadius;
		glm::vec3 mGravity{ 0.0f, -9.81f, 0.0f };
	};
}

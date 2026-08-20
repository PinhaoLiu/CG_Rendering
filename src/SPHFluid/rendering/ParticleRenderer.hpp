#pragma once

#include <glad/glad.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace sph
{
	class ParticleRenderer
	{
	public:
		ParticleRenderer() = default;
		~ParticleRenderer();

		ParticleRenderer(ParticleRenderer const&) = delete;
		ParticleRenderer& operator=(ParticleRenderer const&) = delete;

		void initialize(std::size_t maximum_particle_count);
		void initializeExternalPositionBuffer(GLuint position_buffer,
		                                      std::size_t maximum_particle_count,
		                                      GLsizei stride_bytes);
		void uploadPositions(std::vector<glm::vec3> const& positions);
		void setParticleCount(std::size_t particle_count);
		void render(GLuint program, glm::mat4 const& world_to_clip, float point_size_pixels) const;
		void shutdown() noexcept;

		std::size_t particleCount() const noexcept;

	private:
		GLuint mVertexArray{ 0u };
		GLuint mPositionBuffer{ 0u };
		GLsizei mParticleCount{ 0 };
		std::size_t mCapacity{ 0u };
		bool mOwnsPositionBuffer{ false };
	};
}

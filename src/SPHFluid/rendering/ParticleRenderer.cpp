#include "rendering/ParticleRenderer.hpp"

#include "core/opengl.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <limits>
#include <stdexcept>

namespace sph
{
	ParticleRenderer::~ParticleRenderer()
	{
		shutdown();
	}

	void ParticleRenderer::initialize(std::size_t const maximum_particle_count)
	{
		shutdown();
		if (maximum_particle_count == 0u ||
		    maximum_particle_count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
			throw std::invalid_argument("Particle buffer capacity is outside the OpenGL draw range.");

		glGenVertexArrays(1, &mVertexArray);
		glGenBuffers(1, &mPositionBuffer);

		glBindVertexArray(mVertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, mPositionBuffer);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(maximum_particle_count * sizeof(glm::vec3)),
		             nullptr,
		             GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0u);
		glVertexAttribPointer(0u, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
		utils::opengl::debug::nameObject(GL_VERTEX_ARRAY, mVertexArray, "SPH particle VAO");
		utils::opengl::debug::nameObject(GL_BUFFER, mPositionBuffer, "SPH particle positions");
		glBindVertexArray(0u);
		glBindBuffer(GL_ARRAY_BUFFER, 0u);
		mCapacity = maximum_particle_count;
		mOwnsPositionBuffer = true;
	}

	void ParticleRenderer::initializeExternalPositionBuffer(
		GLuint const position_buffer,
		std::size_t const maximum_particle_count,
		GLsizei const stride_bytes)
	{
		shutdown();
		if (position_buffer == 0u)
			throw std::invalid_argument("External particle position buffer must be valid.");
		if (maximum_particle_count == 0u ||
		    maximum_particle_count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
			throw std::invalid_argument("Particle buffer capacity is outside the OpenGL draw range.");
		if (stride_bytes < static_cast<GLsizei>(3u * sizeof(float)))
			throw std::invalid_argument("External particle position stride is too small.");

		glGenVertexArrays(1, &mVertexArray);
		glBindVertexArray(mVertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
		glEnableVertexAttribArray(0u);
		glVertexAttribPointer(0u, 3, GL_FLOAT, GL_FALSE, stride_bytes, nullptr);
		utils::opengl::debug::nameObject(GL_VERTEX_ARRAY, mVertexArray,
		                                  "SPH GPU particle VAO");
		glBindVertexArray(0u);
		glBindBuffer(GL_ARRAY_BUFFER, 0u);

		mPositionBuffer = position_buffer;
		mCapacity = maximum_particle_count;
		mOwnsPositionBuffer = false;
	}

	void ParticleRenderer::uploadPositions(std::vector<glm::vec3> const& positions)
	{
		if (positions.size() > mCapacity)
			throw std::runtime_error("Particle count exceeds the preallocated GPU capacity.");

		glBindBuffer(GL_ARRAY_BUFFER, mPositionBuffer);
		if (!positions.empty())
			glBufferSubData(GL_ARRAY_BUFFER,
			                0,
			                static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
			                positions.data());
		glBindBuffer(GL_ARRAY_BUFFER, 0u);
		mParticleCount = static_cast<GLsizei>(positions.size());
	}

	void ParticleRenderer::setParticleCount(std::size_t const particle_count)
	{
		if (particle_count > mCapacity)
			throw std::runtime_error("Particle count exceeds the renderer capacity.");
		mParticleCount = static_cast<GLsizei>(particle_count);
	}

	void ParticleRenderer::render(GLuint const program,
	                              glm::mat4 const& world_to_clip,
	                              float const point_size_pixels) const
	{
		if (program == 0u || mVertexArray == 0u || mParticleCount == 0)
			return;

		utils::opengl::debug::beginDebugGroup("SPH particle draw");
		glUseProgram(program);
		glUniformMatrix4fv(glGetUniformLocation(program, "uWorldToClip"),
		                   1, GL_FALSE, glm::value_ptr(world_to_clip));
		glUniform1f(glGetUniformLocation(program, "uPointSizePixels"), point_size_pixels);
		glBindVertexArray(mVertexArray);
		glDrawArrays(GL_POINTS, 0, mParticleCount);
		glBindVertexArray(0u);
		glUseProgram(0u);
		utils::opengl::debug::endDebugGroup();
	}

	void ParticleRenderer::shutdown() noexcept
	{
		if (mOwnsPositionBuffer && mPositionBuffer != 0u)
			glDeleteBuffers(1, &mPositionBuffer);
		if (mVertexArray != 0u)
			glDeleteVertexArrays(1, &mVertexArray);
		mPositionBuffer = 0u;
		mVertexArray = 0u;
		mParticleCount = 0;
		mCapacity = 0u;
		mOwnsPositionBuffer = false;
	}

	std::size_t ParticleRenderer::particleCount() const noexcept
	{
		return static_cast<std::size_t>(mParticleCount);
	}
}

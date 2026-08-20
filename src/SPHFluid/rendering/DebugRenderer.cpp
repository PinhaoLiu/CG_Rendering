#include "rendering/DebugRenderer.hpp"

#include "core/opengl.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <cmath>

namespace sph
{
	DebugRenderer::~DebugRenderer()
	{
		shutdown();
	}

	void DebugRenderer::initialize()
	{
		shutdown();
		mVertices.reserve(kMaximumVertices);

		glGenVertexArrays(1, &mVertexArray);
		glGenBuffers(1, &mVertexBuffer);
		glBindVertexArray(mVertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(kMaximumVertices * sizeof(Vertex)),
		             nullptr,
		             GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0u);
		glVertexAttribPointer(0u, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		                      reinterpret_cast<void const*>(offsetof(Vertex, position)));
		glEnableVertexAttribArray(1u);
		glVertexAttribPointer(1u, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		                      reinterpret_cast<void const*>(offsetof(Vertex, colour)));
		utils::opengl::debug::nameObject(GL_VERTEX_ARRAY, mVertexArray, "SPH debug VAO");
		utils::opengl::debug::nameObject(GL_BUFFER, mVertexBuffer, "SPH debug lines");
		glBindVertexArray(0u);
		glBindBuffer(GL_ARRAY_BUFFER, 0u);
	}

	void DebugRenderer::update(SimulationScene const& scene, bool const emitter_is_valid)
	{
		mVertices.clear();
		appendBox(scene.boundary);
		appendEmitter(scene.emitter, emitter_is_valid);

		glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
		glBufferSubData(GL_ARRAY_BUFFER,
		                0,
		                static_cast<GLsizeiptr>(mVertices.size() * sizeof(Vertex)),
		                mVertices.data());
		glBindBuffer(GL_ARRAY_BUFFER, 0u);
		mVertexCount = static_cast<GLsizei>(mVertices.size());
	}

	void DebugRenderer::render(GLuint const program, glm::mat4 const& world_to_clip) const
	{
		if (program == 0u || mVertexArray == 0u || mVertexCount == 0)
			return;

		utils::opengl::debug::beginDebugGroup("SPH scene debug draw");
		glUseProgram(program);
		glUniformMatrix4fv(glGetUniformLocation(program, "uWorldToClip"),
		                   1, GL_FALSE, glm::value_ptr(world_to_clip));
		glBindVertexArray(mVertexArray);
		glDrawArrays(GL_LINES, 0, mVertexCount);
		glBindVertexArray(0u);
		glUseProgram(0u);
		utils::opengl::debug::endDebugGroup();
	}

	void DebugRenderer::shutdown() noexcept
	{
		if (mVertexBuffer != 0u)
			glDeleteBuffers(1, &mVertexBuffer);
		if (mVertexArray != 0u)
			glDeleteVertexArrays(1, &mVertexArray);
		mVertexBuffer = 0u;
		mVertexArray = 0u;
		mVertexCount = 0;
	}

	void DebugRenderer::appendLine(glm::vec3 const& from,
	                               glm::vec3 const& to,
	                               glm::vec3 const& colour)
	{
		mVertices.push_back({ from, colour });
		mVertices.push_back({ to, colour });
	}

	void DebugRenderer::appendBox(BoxBoundary const& boundary)
	{
		glm::vec3 const colour(0.15f, 0.85f, 0.95f);
		glm::vec3 corners[8];
		for (unsigned index = 0u; index < 8u; ++index) {
			glm::vec3 const sign((index & 1u) ? 1.0f : -1.0f,
			                     (index & 2u) ? 1.0f : -1.0f,
			                     (index & 4u) ? 1.0f : -1.0f);
			corners[index] = boundary.center + sign * boundary.halfExtent;
		}
		for (unsigned index = 0u; index < 8u; ++index) {
			for (unsigned axis_bit : { 1u, 2u, 4u }) {
				if ((index & axis_bit) == 0u)
					appendLine(corners[index], corners[index | axis_bit], colour);
			}
		}
	}

	void DebugRenderer::appendEmitter(EmitterConfig const& emitter, bool const is_valid)
	{
		glm::vec3 const colour = is_valid
			? glm::vec3(1.0f, 0.58f, 0.12f)
			: glm::vec3(1.0f, 0.12f, 0.12f);
		for (unsigned ring = 0u; ring < 3u; ++ring) {
			for (unsigned segment = 0u; segment < kSphereSegments; ++segment) {
				float const angle0 = glm::two_pi<float>() * static_cast<float>(segment) /
				                     static_cast<float>(kSphereSegments);
				float const angle1 = glm::two_pi<float>() * static_cast<float>(segment + 1u) /
				                     static_cast<float>(kSphereSegments);
				glm::vec3 first(0.0f);
				glm::vec3 second(0.0f);
				int const axis0 = static_cast<int>((ring + 1u) % 3u);
				int const axis1 = static_cast<int>((ring + 2u) % 3u);
				first[axis0] = emitter.radius * std::cos(angle0);
				first[axis1] = emitter.radius * std::sin(angle0);
				second[axis0] = emitter.radius * std::cos(angle1);
				second[axis1] = emitter.radius * std::sin(angle1);
				appendLine(emitter.position + first, emitter.position + second, colour);
			}
		}
	}
}

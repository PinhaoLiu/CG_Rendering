#pragma once

#include "simulation/SimulationScene.hpp"

#include <glad/glad.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace sph
{
	class DebugRenderer
	{
	public:
		DebugRenderer() = default;
		~DebugRenderer();

		DebugRenderer(DebugRenderer const&) = delete;
		DebugRenderer& operator=(DebugRenderer const&) = delete;

		void initialize();
		void update(SimulationScene const& scene, bool emitter_is_valid);
		void render(GLuint program, glm::mat4 const& world_to_clip) const;
		void shutdown() noexcept;

	private:
		struct Vertex
		{
			glm::vec3 position{ 0.0f };
			glm::vec3 colour{ 1.0f };
		};

		void appendLine(glm::vec3 const& from, glm::vec3 const& to, glm::vec3 const& colour);
		void appendBox(BoxBoundary const& boundary);
		void appendEmitter(EmitterConfig const& emitter, bool is_valid);

		std::vector<Vertex> mVertices;
		GLuint mVertexArray{ 0u };
		GLuint mVertexBuffer{ 0u };
		GLsizei mVertexCount{ 0 };

		static constexpr unsigned kSphereSegments = 48u;
		static constexpr std::size_t kMaximumVertices = 24u + 6u * kSphereSegments;
	};
}

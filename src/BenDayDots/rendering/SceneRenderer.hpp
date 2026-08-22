#pragma once

#include "core/helpers.hpp"

#include <glm/mat4x4.hpp>

#include <vector>

namespace benday
{
	struct GeometryTextureData
	{
		GLuint diffuseTexture{ 0u };
		GLuint opacityTexture{ 0u };
	};

	struct SceneShaderLocations
	{
		GLint vertexModelToWorld{ -1 };
		GLint vertexWorldToClip{ -1 };
		GLint normalModelToWorld{ -1 };
		GLint lightDirection{ -1 };
		GLint diffuseTexture{ -1 };
		GLint opacityTexture{ -1 };
		GLint hasDiffuseTexture{ -1 };
		GLint hasOpacityTexture{ -1 };
		GLint materialDiffuse{ -1 };
	};

	class SceneRenderer
	{
	public:
		SceneRenderer() = default;
		~SceneRenderer();

		SceneRenderer(SceneRenderer const&) = delete;
		SceneRenderer& operator=(SceneRenderer const&) = delete;

		bool initialize();
		void shutdown();
		void invalidateShaderLocations();
		void render(GLuint program, glm::mat4 const& world_to_clip) const;

	private:
		void cacheShaderLocations(GLuint program) const;

		std::vector<bonobo::mesh_data> mMeshes;
		std::vector<GeometryTextureData> mTextureData;
		GLuint mDefaultSampler{ 0u };
		GLuint mMipmapSampler{ 0u };
		GLuint mDebugTexture{ 0u };
		mutable GLuint mLocationsProgram{ 0u };
		mutable SceneShaderLocations mLocations;
	};
}

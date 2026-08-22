#include "rendering/SceneRenderer.hpp"

#include "config.hpp"
#include "core/Log.h"

#include <glm/gtc/type_ptr.hpp>

#include <unordered_set>

namespace
{
	GLuint findTexture(bonobo::mesh_data const& mesh, char const* const name)
	{
		auto const texture = mesh.bindings.find(name);
		return texture != mesh.bindings.end() ? texture->second : 0u;
	}
}

namespace benday
{
	SceneRenderer::~SceneRenderer()
	{
		shutdown();
	}

	bool SceneRenderer::initialize()
	{
		shutdown();
		mMeshes = bonobo::loadObjects(config::resources_path("sponza/sponza.obj"));
		if (mMeshes.empty()) {
			LogError("Failed to load Sponza for the Ben-Day Dots scene.");
			return false;
		}

		// Match Assignment 2: cache the material texture IDs once after importing
		// the model instead of looking up string bindings for every draw call.
		mTextureData.reserve(mMeshes.size());
		for (auto const& mesh : mMeshes) {
			GeometryTextureData data;
			data.diffuseTexture = findTexture(mesh, "diffuse_texture");
			data.opacityTexture = findTexture(mesh, "opacity_texture");
			mTextureData.emplace_back(data);
		}

		// The framework owns this complete placeholder texture.  Assignment 2
		// binds it whenever a Sponza material has no texture for a sampled unit.
		mDebugTexture = bonobo::getDebugTextureID();
		if (mDebugTexture == 0u) {
			LogError("The framework debug texture is unavailable.");
			shutdown();
			return false;
		}

		glGenSamplers(1, &mDefaultSampler);
		glSamplerParameteri(mDefaultSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glSamplerParameteri(mDefaultSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(mDefaultSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glSamplerParameteri(mDefaultSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glGenSamplers(1, &mMipmapSampler);
		glSamplerParameteri(mMipmapSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glSamplerParameteri(mMipmapSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(mMipmapSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glSamplerParameteri(mMipmapSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
		return true;
	}

	void SceneRenderer::shutdown()
	{
		std::unordered_set<GLuint> textures;
		for (auto const& mesh : mMeshes) {
			for (auto const& binding : mesh.bindings) {
				if (binding.second != 0u)
					textures.insert(binding.second);
			}
			if (mesh.vao != 0u)
				glDeleteVertexArrays(1, &mesh.vao);
			if (mesh.bo != 0u)
				glDeleteBuffers(1, &mesh.bo);
			if (mesh.ibo != 0u)
				glDeleteBuffers(1, &mesh.ibo);
		}
		if (!textures.empty()) {
			std::vector<GLuint> const texture_names(textures.begin(), textures.end());
			glDeleteTextures(static_cast<GLsizei>(texture_names.size()), texture_names.data());
		}
		mMeshes.clear();
		mTextureData.clear();

		if (mDefaultSampler != 0u)
			glDeleteSamplers(1, &mDefaultSampler);
		mDefaultSampler = 0u;
		if (mMipmapSampler != 0u)
			glDeleteSamplers(1, &mMipmapSampler);
		mMipmapSampler = 0u;
		mDebugTexture = 0u; // Owned by bonobo::init()/bonobo::deinit().
		mLocationsProgram = 0u;
		mLocations = {};
	}

	void SceneRenderer::invalidateShaderLocations()
	{
		mLocationsProgram = 0u;
		mLocations = {};
	}

	void SceneRenderer::cacheShaderLocations(GLuint const program) const
	{
		if (program == mLocationsProgram)
			return;

		mLocations.vertexModelToWorld = glGetUniformLocation(program, "vertex_model_to_world");
		mLocations.vertexWorldToClip = glGetUniformLocation(program, "vertex_world_to_clip");
		mLocations.normalModelToWorld = glGetUniformLocation(program, "normal_model_to_world");
		mLocations.lightDirection = glGetUniformLocation(program, "light_direction");
		mLocations.diffuseTexture = glGetUniformLocation(program, "diffuse_texture");
		mLocations.opacityTexture = glGetUniformLocation(program, "opacity_texture");
		mLocations.hasDiffuseTexture = glGetUniformLocation(program, "has_diffuse_texture");
		mLocations.hasOpacityTexture = glGetUniformLocation(program, "has_opacity_texture");
		mLocations.materialDiffuse = glGetUniformLocation(program, "material_diffuse");
		mLocationsProgram = program;
	}

	void SceneRenderer::render(GLuint const program,
	                           glm::mat4 const& world_to_clip) const
	{
		if (program == 0u)
			return;
		if (mTextureData.size() != mMeshes.size() || mDebugTexture == 0u)
			return;

		cacheShaderLocations(program);
		glm::mat4 const model_to_world(1.0f);
		glm::mat3 const normal_model_to_world(1.0f);
		glUseProgram(program);
		glUniformMatrix4fv(mLocations.vertexModelToWorld,
		                   1, GL_FALSE, glm::value_ptr(model_to_world));
		glUniformMatrix4fv(mLocations.vertexWorldToClip,
		                   1, GL_FALSE, glm::value_ptr(world_to_clip));
		glUniformMatrix3fv(mLocations.normalModelToWorld,
		                   1, GL_FALSE, glm::value_ptr(normal_model_to_world));
		glUniform3f(mLocations.lightDirection, 0.35f, 0.8f, 0.25f);
		glUniform1i(mLocations.diffuseTexture, 0);
		glUniform1i(mLocations.opacityTexture, 1);

		for (std::size_t index = 0u; index < mMeshes.size(); ++index) {
			auto const& mesh = mMeshes[index];
			auto const& texture_data = mTextureData[index];
			GLuint const diffuse_texture = texture_data.diffuseTexture;
			GLuint const opacity_texture = texture_data.opacityTexture;
			glUniform1i(mLocations.hasDiffuseTexture,
			            diffuse_texture != 0u ? 1 : 0);
			glUniform1i(mLocations.hasOpacityTexture,
			            opacity_texture != 0u ? 1 : 0);
			glUniform3fv(mLocations.materialDiffuse,
			             1, glm::value_ptr(mesh.material.diffuse));

			glBindSampler(0u, diffuse_texture != 0u ? mMipmapSampler : mDefaultSampler);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D,
			              diffuse_texture != 0u ? diffuse_texture : mDebugTexture);
			glBindSampler(1u, opacity_texture != 0u ? mMipmapSampler : mDefaultSampler);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D,
			              opacity_texture != 0u ? opacity_texture : mDebugTexture);

			glBindVertexArray(mesh.vao);
			if (mesh.ibo != 0u) {
				glDrawElements(mesh.drawing_mode, mesh.indices_nb, GL_UNSIGNED_INT,
				               reinterpret_cast<GLvoid const*>(0x0));
			} else {
				glDrawArrays(mesh.drawing_mode, 0, mesh.vertices_nb);
			}
		}

		glBindVertexArray(0u);
		// Disable the sampling program before clearing texture bindings.  Leaving
		// it active while binding object 0 was the source of the unit 1 warning.
		glUseProgram(0u);
		glBindSampler(1u, 0u);
		glBindSampler(0u, 0u);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0u);
	}
}

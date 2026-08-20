#include "rendering/ScreenSpaceFluidRenderer.hpp"

#include "core/opengl.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace sph
{
	namespace
	{
		void configureTexture(GLuint const texture,
		                      GLenum const internal_format,
		                      int const width,
		                      int const height,
		                      GLenum const minimum_filter,
		                      char const* const label)
		{
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minimum_filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, minimum_filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexStorage2D(GL_TEXTURE_2D, 1, internal_format, width, height);
			utils::opengl::debug::nameObject(
				GL_TEXTURE, texture,
				std::string(label) + " " + std::to_string(width) + "x" +
				std::to_string(height));
		}

		void setMatrix(GLuint const program, char const* const name, glm::mat4 const& value)
		{
			glUniformMatrix4fv(glGetUniformLocation(program, name),
			                   1, GL_FALSE, glm::value_ptr(value));
		}
	}

	ScreenSpaceFluidRenderer::~ScreenSpaceFluidRenderer()
	{
		shutdown();
	}

	void ScreenSpaceFluidRenderer::initialize(ScreenSpaceFluidPrograms const& programs,
	                                           int const width,
	                                           int const height)
	{
		shutdown();
		setPrograms(programs);
		glGenVertexArrays(1, &mParticleVertexArray);
		glGenVertexArrays(1, &mFullscreenVertexArray);
		// glGenVertexArrays only reserves names. Binding once creates the objects
		// before KHR_debug labels are assigned.
		glBindVertexArray(mParticleVertexArray);
		glBindVertexArray(mFullscreenVertexArray);
		glBindVertexArray(0u);
		utils::opengl::debug::nameObject(GL_VERTEX_ARRAY, mParticleVertexArray,
		                                  "SPH instanced sphere VAO");
		utils::opengl::debug::nameObject(GL_VERTEX_ARRAY, mFullscreenVertexArray,
		                                  "SPH fullscreen triangle VAO");
		for (TimingQuerySlot& slot : mTimingSlots)
			glGenQueries(static_cast<GLsizei>(slot.timestamps.size()), slot.timestamps.data());
		mInitialized = true;
		resize(width, height);
	}

	void ScreenSpaceFluidRenderer::setPrograms(ScreenSpaceFluidPrograms const& programs)
	{
		if (programs.rawDepth == 0u || programs.rawThickness == 0u ||
		    programs.bilateralFilter == 0u || programs.reconstructNormal == 0u ||
		    programs.present == 0u)
			throw std::invalid_argument("All screen-space fluid programs must be valid.");
		mPrograms = programs;
	}

	void ScreenSpaceFluidRenderer::shutdown() noexcept
	{
		if (mInitialized) {
			for (TimingQuerySlot& slot : mTimingSlots) {
				glDeleteQueries(static_cast<GLsizei>(slot.timestamps.size()), slot.timestamps.data());
				slot = {};
			}
		}
		destroyTargets();
		if (mParticleVertexArray != 0u)
			glDeleteVertexArrays(1, &mParticleVertexArray);
		if (mFullscreenVertexArray != 0u)
			glDeleteVertexArrays(1, &mFullscreenVertexArray);
		mParticleVertexArray = 0u;
		mFullscreenVertexArray = 0u;
		mPrograms = {};
		mWidth = 0;
		mHeight = 0;
		mInitialized = false;
		mSceneActive = false;
		mActiveTimingSlot = nullptr;
		mTimings = {};
		mRawDepthStats = {};
		mThicknessStats = {};
		mPathHistories = {};
	}

	void ScreenSpaceFluidRenderer::resize(int const width, int const height)
	{
		requireInitialized();
		if (width <= 0 || height <= 0)
			return;
		if (width == mWidth && height == mHeight && mFramebuffersComplete)
			return;
		mWidth = width;
		mHeight = height;
		mPathHistories = {};
		mRawDepthStats = {};
		mThicknessStats = {};
		destroyTargets();
		createTargets();
	}

	void ScreenSpaceFluidRenderer::beginScene(int const width,
	                                          int const height,
	                                          glm::vec3 const& clear_colour)
	{
		requireInitialized();
		if (mSceneActive)
			throw std::logic_error("The scene pass is already active.");
		resize(width, height);
		if (!mFramebuffersComplete)
			throw std::runtime_error("Screen-space fluid render targets are incomplete.");
		pollTimingQueries();
		mActiveTimingSlot = acquireTimingSlot();
		mNextTimestampIndex = 0u;
		recordStage(0u);

		glBindFramebuffer(GL_FRAMEBUFFER, mSceneFramebuffer);
		glViewport(0, 0, mWidth, mHeight);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClearColor(clear_colour.r, clear_colour.g, clear_colour.b, 1.0f);
		glClearDepth(1.0);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		mSceneActive = true;
	}

	void ScreenSpaceFluidRenderer::endScene()
	{
		requireInitialized();
		if (!mSceneActive)
			throw std::logic_error("The scene pass is not active.");
		recordStage(1u);
		mSceneActive = false;
	}

	void ScreenSpaceFluidRenderer::renderRawDepth(
		GLuint const position_buffer,
		std::size_t const particle_count,
		float const particle_radius,
		glm::mat4 const& world_to_view,
		glm::mat4 const& view_to_clip,
		glm::mat4 const& clip_to_view)
	{
		requireInitialized();
		if (mSceneActive)
			throw std::logic_error("End the scene pass before rendering fluid depth.");
		if (position_buffer == 0u)
			throw std::invalid_argument("Fluid depth rendering requires a valid position SSBO.");
		if (particle_count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
			throw std::length_error("Particle count exceeds the instanced draw range.");
		if (!(particle_radius > 0.0f))
			throw std::invalid_argument("Particle radius must be positive.");

		glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mRawDepthFramebuffer);
		glBlitFramebuffer(0, 0, mWidth, mHeight,
		                  0, 0, mWidth, mHeight,
		                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_FRAMEBUFFER, mRawDepthFramebuffer);
		glViewport(0, 0, mWidth, mHeight);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		GLfloat const clear_depth[] = { kDepthSentinel, 0.0f, 0.0f, 0.0f };
		glClearBufferfv(GL_COLOR, 0, clear_depth);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);

		if (particle_count != 0u) {
			utils::opengl::debug::beginDebugGroup("Fluid analytic sphere depth");
			glUseProgram(mPrograms.rawDepth);
			setMatrix(mPrograms.rawDepth, "uWorldToView", world_to_view);
			setMatrix(mPrograms.rawDepth, "uViewToClip", view_to_clip);
			setMatrix(mPrograms.rawDepth, "uClipToView", clip_to_view);
			glUniform1f(glGetUniformLocation(mPrograms.rawDepth, "uParticleRadius"),
			            particle_radius);
			glUniform2f(glGetUniformLocation(mPrograms.rawDepth, "uViewportSize"),
			            static_cast<float>(mWidth), static_cast<float>(mHeight));
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, position_buffer);
			glBindVertexArray(mParticleVertexArray);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
			                      static_cast<GLsizei>(particle_count));
			glBindVertexArray(0u);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, 0u);
			glUseProgram(0u);
			utils::opengl::debug::endDebugGroup();
		}
		recordStage(2u);
	}

	void ScreenSpaceFluidRenderer::renderRawThickness(
		GLuint const position_buffer,
		std::size_t const particle_count,
		float const particle_radius,
		float const thickness_scale,
		glm::mat4 const& world_to_view,
		glm::mat4 const& view_to_clip,
		glm::mat4 const& clip_to_view)
	{
		requireInitialized();
		if (mNextTimestampIndex <= 2u) recordStage(2u);
		if (mSceneActive)
			throw std::logic_error("End the scene pass before rendering fluid thickness.");
		if (position_buffer == 0u)
			throw std::invalid_argument("Fluid thickness rendering requires a valid position SSBO.");
		if (particle_count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
			throw std::length_error("Particle count exceeds the instanced draw range.");
		if (!(particle_radius > 0.0f) || !(thickness_scale >= 0.0f) ||
		    !std::isfinite(thickness_scale))
			throw std::invalid_argument("Fluid thickness radius or scale is invalid.");

		// Restore opaque scene depth. RawDepth wrote nearest-fluid hardware depth
		// into the shared working attachment, which must not reject back layers.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mRawThicknessFramebuffer);
		glBlitFramebuffer(0, 0, mWidth, mHeight,
		                  0, 0, mWidth, mHeight,
		                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_FRAMEBUFFER, mRawThicknessFramebuffer);
		glViewport(0, 0, mWidth, mHeight);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		GLfloat const clear_thickness[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		glClearBufferfv(GL_COLOR, 0, clear_thickness);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);

		if (particle_count != 0u && thickness_scale > 0.0f) {
			utils::opengl::debug::beginDebugGroup("Fluid additive analytic sphere thickness");
			glUseProgram(mPrograms.rawThickness);
			setMatrix(mPrograms.rawThickness, "uWorldToView", world_to_view);
			setMatrix(mPrograms.rawThickness, "uViewToClip", view_to_clip);
			setMatrix(mPrograms.rawThickness, "uClipToView", clip_to_view);
			glUniform1f(glGetUniformLocation(mPrograms.rawThickness, "uParticleRadius"),
			            particle_radius);
			glUniform1f(glGetUniformLocation(mPrograms.rawThickness, "uThicknessScale"),
			            thickness_scale);
			glUniform2f(glGetUniformLocation(mPrograms.rawThickness, "uViewportSize"),
			            static_cast<float>(mWidth), static_cast<float>(mHeight));
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, position_buffer);
			glBindVertexArray(mParticleVertexArray);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
			                      static_cast<GLsizei>(particle_count));
			glBindVertexArray(0u);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, 0u);
			glUseProgram(0u);
			utils::opengl::debug::endDebugGroup();
		}
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		recordStage(3u);
	}

	void ScreenSpaceFluidRenderer::smoothDepth(
		BilateralSmoothingParameters const& parameters,
		glm::mat4 const& view_to_clip)
	{
		if (mNextTimestampIndex <= 3u) recordStage(3u);
		filterScalarField(mRawDepthTexture, mRawDepthTexture, mSmoothDepthTextures,
		                  true, parameters, view_to_clip, "Fluid smooth depth");
		recordStage(4u);
	}

	void ScreenSpaceFluidRenderer::smoothThickness(
		BilateralSmoothingParameters const& parameters,
		glm::mat4 const& view_to_clip)
	{
		if (mNextTimestampIndex <= 4u) recordStage(4u);
		filterScalarField(mRawThicknessTexture, mRawDepthTexture,
		                  mSmoothThicknessTextures, false, parameters,
		                  view_to_clip, "Fluid smooth thickness");
		recordStage(5u);
	}

	void ScreenSpaceFluidRenderer::reconstructNormals(
		NormalDepthSource const depth_source,
		NormalOutputSpace const output_space,
		glm::mat4 const& clip_to_view,
		glm::mat4 const& view_to_world)
	{
		requireInitialized();
		if (mSceneActive)
			throw std::logic_error("End the scene pass before reconstructing normals.");
		while (mNextTimestampIndex <= 5u)
			recordStage(mNextTimestampIndex);

		GLuint const depth_texture = depth_source == NormalDepthSource::Smoothed
			? mSmoothDepthTextures[1] : mRawDepthTexture;
		utils::opengl::debug::beginDebugGroup(
			output_space == NormalOutputSpace::World
				? "Fluid reconstruct world-space normal"
				: "Fluid reconstruct view-space normal");
		glBindFramebuffer(GL_FRAMEBUFFER, mFilterFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       mNormalTexture, 0);
		glViewport(0, 0, mWidth, mHeight);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glUseProgram(mPrograms.reconstructNormal);
		setMatrix(mPrograms.reconstructNormal, "uClipToView", clip_to_view);
		setMatrix(mPrograms.reconstructNormal, "uViewToWorld", view_to_world);
		glUniform1f(glGetUniformLocation(mPrograms.reconstructNormal, "uDepthSentinel"),
		            kDepthSentinel);
		glUniform1i(glGetUniformLocation(mPrograms.reconstructNormal, "uOutputWorldSpace"),
		            output_space == NormalOutputSpace::World ? 1 : 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, depth_texture);
		glBindVertexArray(mFullscreenVertexArray);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glUseProgram(0u);
		glDepthMask(GL_TRUE);
		utils::opengl::debug::endDebugGroup();
		recordStage(6u);
	}

	void ScreenSpaceFluidRenderer::filterScalarField(
		GLuint const source_texture,
		GLuint const guide_depth_texture,
		std::array<GLuint, 2u> const& targets,
		bool const depth_field,
		BilateralSmoothingParameters const& parameters,
		glm::mat4 const& view_to_clip,
		char const* const debug_label)
	{
		requireInitialized();
		if (mSceneActive)
			throw std::logic_error("End the scene pass before bilateral filtering.");
		if (!(parameters.worldRadiusMetres > 0.0f) ||
		    !(parameters.spatialSigmaFactor > 0.0f) ||
		    !(parameters.depthFalloffPerMetre >= 0.0f) ||
		    parameters.maximumRadiusPixels < 1 || parameters.maximumRadiusPixels > 32 ||
		    parameters.iterations < 1 || parameters.iterations > 5)
			throw std::invalid_argument("Bilateral smoothing parameters are outside supported bounds.");

		glBindFramebuffer(GL_FRAMEBUFFER, mFilterFramebuffer);
		glViewport(0, 0, mWidth, mHeight);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glUseProgram(mPrograms.bilateralFilter);
		glUniform2f(glGetUniformLocation(mPrograms.bilateralFilter, "uViewportSize"),
		            static_cast<float>(mWidth), static_cast<float>(mHeight));
		glUniform1f(glGetUniformLocation(mPrograms.bilateralFilter, "uWorldRadiusMetres"),
		            parameters.worldRadiusMetres);
		glUniform1f(glGetUniformLocation(mPrograms.bilateralFilter, "uSpatialSigmaFactor"),
		            parameters.spatialSigmaFactor);
		glUniform1f(glGetUniformLocation(mPrograms.bilateralFilter, "uDepthFalloffPerMetre"),
		            parameters.depthFalloffPerMetre);
		glUniform1i(glGetUniformLocation(mPrograms.bilateralFilter, "uMaximumRadiusPixels"),
		            parameters.maximumRadiusPixels);
		glUniform1f(glGetUniformLocation(mPrograms.bilateralFilter, "uProjectionScaleY"),
		            std::abs(view_to_clip[1][1]));
		glUniform1f(glGetUniformLocation(mPrograms.bilateralFilter, "uDepthSentinel"),
		            kDepthSentinel);
		glUniform1i(glGetUniformLocation(mPrograms.bilateralFilter, "uDepthField"),
		            depth_field ? 1 : 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, guide_depth_texture);
		glBindVertexArray(mFullscreenVertexArray);

		GLuint current_source = source_texture;
		for (int iteration = 0; iteration < parameters.iterations; ++iteration) {
			for (int axis = 0; axis < 2; ++axis) {
				GLuint const target = targets[static_cast<std::size_t>(axis)];
				utils::opengl::debug::beginDebugGroup(
					std::string(debug_label) + (axis == 0 ? " horizontal #" : " vertical #") +
					std::to_string(iteration + 1));
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
				                       target, 0);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, current_source);
				glUniform2i(glGetUniformLocation(mPrograms.bilateralFilter, "uDirection"),
				            axis == 0 ? 1 : 0, axis == 0 ? 0 : 1);
				glDrawArrays(GL_TRIANGLES, 0, 3);
				current_source = target;
				utils::opengl::debug::endDebugGroup();
			}
		}
		glBindVertexArray(0u);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glUseProgram(0u);
		glDepthMask(GL_TRUE);
	}

	void ScreenSpaceFluidRenderer::present(FluidDisplayMode const mode,
	                                       float const debug_depth_scale,
	                                       float const debug_thickness_scale,
	                                       FluidMaterialParameters const& material,
	                                       glm::mat4 const& clip_to_view,
	                                       glm::mat4 const& view_to_clip,
	                                       glm::mat4 const& view_to_world)
	{
		requireInitialized();
		if (mSceneActive)
			throw std::logic_error("End the scene pass before presentation.");
		auto const finite_nonnegative = [](glm::vec3 const& value) {
			return std::isfinite(value.x) && std::isfinite(value.y) &&
			       std::isfinite(value.z) && value.x >= 0.0f &&
			       value.y >= 0.0f && value.z >= 0.0f;
		};
		if (!(material.indexOfRefraction > 1.0f) ||
		    !std::isfinite(material.indexOfRefraction) ||
		    !finite_nonnegative(material.absorptionPerMetre) ||
		    !finite_nonnegative(material.scatteringColour) ||
		    !(material.refractionScale >= 0.0f) ||
		    !(material.roughness >= 0.0f && material.roughness <= 1.0f) ||
		    !std::isfinite(material.refractionScale))
			throw std::invalid_argument("Fluid material parameters are invalid.");
		while (mNextTimestampIndex <= 6u)
			recordStage(mNextTimestampIndex);

		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		glViewport(0, 0, mWidth, mHeight);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_ALWAYS);
		glDepthMask(GL_TRUE);
		glUseProgram(mPrograms.present);
		setMatrix(mPrograms.present, "uClipToView", clip_to_view);
		setMatrix(mPrograms.present, "uViewToClip", view_to_clip);
		setMatrix(mPrograms.present, "uViewToWorld", view_to_world);
		glUniform1i(glGetUniformLocation(mPrograms.present, "uDisplayMode"),
		            static_cast<int>(mode));
		glUniform1f(glGetUniformLocation(mPrograms.present, "uDebugDepthScale"),
		            std::max(0.001f, debug_depth_scale));
		glUniform1f(glGetUniformLocation(mPrograms.present, "uDebugThicknessScale"),
		            std::max(0.001f, debug_thickness_scale));
		glUniform1f(glGetUniformLocation(mPrograms.present, "uDepthSentinel"),
		            kDepthSentinel);
		glUniform1f(glGetUniformLocation(mPrograms.present, "uIndexOfRefraction"),
		            material.indexOfRefraction);
		glUniform3fv(glGetUniformLocation(mPrograms.present, "uAbsorptionPerMetre"),
		             1, &material.absorptionPerMetre.x);
		glUniform3fv(glGetUniformLocation(mPrograms.present, "uScatteringColour"),
		             1, &material.scatteringColour.x);
		glUniform1f(glGetUniformLocation(mPrograms.present, "uRefractionScale"),
		            material.refractionScale);
		glUniform1f(glGetUniformLocation(mPrograms.present, "uRoughness"),
		            material.roughness);
		glUniform1i(glGetUniformLocation(mPrograms.present, "uReflectionEnabled"),
		            material.reflectionEnabled ? 1 : 0);
		glUniform1i(glGetUniformLocation(mPrograms.present, "uRefractionEnabled"),
		            material.refractionEnabled ? 1 : 0);
		glUniform1i(glGetUniformLocation(mPrograms.present, "uAbsorptionEnabled"),
		            material.absorptionEnabled ? 1 : 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, mSceneColourTexture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, mRawDepthTexture);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, mRawThicknessTexture);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, mSmoothDepthTextures[1]);
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, mSmoothThicknessTextures[1]);
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, mNormalTexture);
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, mSceneDepthTexture);
		glBindVertexArray(mFullscreenVertexArray);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0u);
		for (int unit = 6; unit >= 0; --unit) {
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, 0u);
		}
		glUseProgram(0u);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		recordStage(7u);
		if (mActiveTimingSlot != nullptr) {
			mActiveTimingSlot->mode = mode;
			mActiveTimingSlot->pending = true;
		}
		mActiveTimingSlot = nullptr;
		mNextTimestampIndex = 0u;
	}

	int ScreenSpaceFluidRenderer::width() const noexcept { return mWidth; }
	int ScreenSpaceFluidRenderer::height() const noexcept { return mHeight; }
	bool ScreenSpaceFluidRenderer::framebuffersComplete() const noexcept { return mFramebuffersComplete; }
	std::size_t ScreenSpaceFluidRenderer::reservedTextureBytes() const noexcept
	{
		if (mWidth <= 0 || mHeight <= 0) return 0u;
		// Full-resolution allocation: scene colour/depth (12 B), raw depth
		// and its hardware-depth occlusion copy (8 B), smooth depth ping/pong
		// (8 B), normal RGB16F (6 B), thickness raw/ping/pong R16F (6 B).
		return static_cast<std::size_t>(mWidth) * static_cast<std::size_t>(mHeight) * 40u;
	}
	GLuint ScreenSpaceFluidRenderer::sceneFramebuffer() const noexcept { return mSceneFramebuffer; }
	GLuint ScreenSpaceFluidRenderer::sceneColourTexture() const noexcept { return mSceneColourTexture; }
	GLuint ScreenSpaceFluidRenderer::sceneDepthTexture() const noexcept { return mSceneDepthTexture; }
	GLuint ScreenSpaceFluidRenderer::rawDepthTexture() const noexcept { return mRawDepthTexture; }
	GLuint ScreenSpaceFluidRenderer::smoothDepthTexture(std::size_t const index) const
	{
		if (index >= mSmoothDepthTextures.size())
			throw std::out_of_range("Smooth-depth ping/pong index must be 0 or 1.");
		return mSmoothDepthTextures[index];
	}
	GLuint ScreenSpaceFluidRenderer::normalTexture() const noexcept { return mNormalTexture; }
	GLuint ScreenSpaceFluidRenderer::rawThicknessTexture() const noexcept { return mRawThicknessTexture; }
	GLuint ScreenSpaceFluidRenderer::smoothThicknessTexture(std::size_t const index) const
	{
		if (index >= mSmoothThicknessTextures.size())
			throw std::out_of_range("Smooth-thickness ping/pong index must be 0 or 1.");
		return mSmoothThicknessTextures[index];
	}
	ScreenSpaceRenderTimings const& ScreenSpaceFluidRenderer::timings() const noexcept { return mTimings; }
	RawDepthStats const& ScreenSpaceFluidRenderer::rawDepthStats() const noexcept { return mRawDepthStats; }
	ThicknessStats const& ScreenSpaceFluidRenderer::thicknessStats() const noexcept { return mThicknessStats; }
	RenderPathStats ScreenSpaceFluidRenderer::renderPathStats(FluidDisplayMode const mode) const
	{
		std::size_t const path_index = mode == FluidDisplayMode::Points ? 0u : 1u;
		PathHistory const& history = mPathHistories[path_index];
		RenderPathStats result;
		result.sampleCount = history.sampleCount;
		result.sceneMedianMilliseconds = percentile(history.sceneSamples, history.sampleCount, 0.50);
		result.sceneP95Milliseconds = percentile(history.sceneSamples, history.sampleCount, 0.95);
		result.wholeMedianMilliseconds = percentile(history.wholeSamples, history.sampleCount, 0.50);
		result.wholeP95Milliseconds = percentile(history.wholeSamples, history.sampleCount, 0.95);
		result.framebufferWidth = history.framebufferWidth;
		result.framebufferHeight = history.framebufferHeight;
		return result;
	}

	void ScreenSpaceFluidRenderer::refreshRawDepthStats()
	{
		requireInitialized();
		std::vector<float> pixels(static_cast<std::size_t>(mWidth) *
		                          static_cast<std::size_t>(mHeight));
		glBindTexture(GL_TEXTURE_2D, mRawDepthTexture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0u);

		RawDepthStats result;
		result.nearestMetres = std::numeric_limits<float>::max();
		for (float const depth : pixels) {
			if (std::isfinite(depth) && depth > 0.0f && depth < 0.5f * kDepthSentinel) {
				result.nearestMetres = std::min(result.nearestMetres, depth);
				result.farthestMetres = std::max(result.farthestMetres, depth);
				++result.coveredPixelCount;
			} else if (!std::isfinite(depth)) {
				++result.invalidPixelCount;
			}
		}
		if (result.coveredPixelCount == 0u)
			result.nearestMetres = 0.0f;
		result.fresh = true;
		mRawDepthStats = result;
	}

	void ScreenSpaceFluidRenderer::refreshThicknessStats(bool const smoothed)
	{
		requireInitialized();
		std::vector<float> pixels(static_cast<std::size_t>(mWidth) *
		                          static_cast<std::size_t>(mHeight));
		GLuint const texture = smoothed ? mSmoothThicknessTextures[1]
		                                : mRawThicknessTexture;
		glBindTexture(GL_TEXTURE_2D, texture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0u);

		ThicknessStats result;
		double sum = 0.0;
		for (float const thickness : pixels) {
			if (!std::isfinite(thickness) || thickness < 0.0f) {
				++result.invalidPixelCount;
				continue;
			}
			if (thickness <= 0.0f) continue;
			result.maximumMetres = std::max(result.maximumMetres, thickness);
			sum += thickness;
			++result.coveredPixelCount;
			if (thickness >= 65000.0f) ++result.saturatedPixelCount;
		}
		if (result.coveredPixelCount != 0u)
			result.meanMetres = static_cast<float>(
				sum / static_cast<double>(result.coveredPixelCount));
		result.smoothed = smoothed;
		result.fresh = true;
		mThicknessStats = result;
	}

	void ScreenSpaceFluidRenderer::synchronizeTimings()
	{
		requireInitialized();
		glFinish();
		pollTimingQueries();
	}

	void ScreenSpaceFluidRenderer::requireInitialized() const
	{
		if (!mInitialized)
			throw std::logic_error("The fluid renderer must be initialized after creating a GL context.");
	}

	void ScreenSpaceFluidRenderer::destroyTargets() noexcept
	{
		GLuint textures[] = { mSceneColourTexture, mSceneDepthTexture,
		                      mRawDepthTexture, mRawHardwareDepthTexture,
		                      mSmoothDepthTextures[0], mSmoothDepthTextures[1],
		                      mNormalTexture, mRawThicknessTexture,
		                      mSmoothThicknessTextures[0], mSmoothThicknessTextures[1] };
		glDeleteTextures(10, textures);
		GLuint framebuffers[] = { mSceneFramebuffer, mRawDepthFramebuffer,
		                          mRawThicknessFramebuffer, mFilterFramebuffer };
		glDeleteFramebuffers(4, framebuffers);
		mSceneColourTexture = 0u;
		mSceneDepthTexture = 0u;
		mRawDepthTexture = 0u;
		mRawHardwareDepthTexture = 0u;
		mSmoothDepthTextures = {};
		mNormalTexture = 0u;
		mRawThicknessTexture = 0u;
		mSmoothThicknessTextures = {};
		mSceneFramebuffer = 0u;
		mRawDepthFramebuffer = 0u;
		mRawThicknessFramebuffer = 0u;
		mFilterFramebuffer = 0u;
		mFramebuffersComplete = false;
	}

	void ScreenSpaceFluidRenderer::createTargets()
	{
		glGenTextures(1, &mSceneColourTexture);
		glGenTextures(1, &mSceneDepthTexture);
		glGenTextures(1, &mRawDepthTexture);
		glGenTextures(1, &mRawHardwareDepthTexture);
		glGenTextures(static_cast<GLsizei>(mSmoothDepthTextures.size()),
		              mSmoothDepthTextures.data());
		glGenTextures(1, &mNormalTexture);
		glGenTextures(1, &mRawThicknessTexture);
		glGenTextures(static_cast<GLsizei>(mSmoothThicknessTextures.size()),
		              mSmoothThicknessTextures.data());
		configureTexture(mSceneColourTexture, GL_RGBA16F, mWidth, mHeight, GL_LINEAR,
		                 "Fluid scene colour RGBA16F");
		configureTexture(mSceneDepthTexture, GL_DEPTH_COMPONENT32F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid scene depth D32F");
		configureTexture(mRawDepthTexture, GL_R32F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid raw linear depth R32F");
		configureTexture(mRawHardwareDepthTexture, GL_DEPTH_COMPONENT32F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid raw hardware depth D32F");
		configureTexture(mSmoothDepthTextures[0], GL_R32F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid smooth depth ping R32F");
		configureTexture(mSmoothDepthTextures[1], GL_R32F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid smooth depth pong R32F");
		configureTexture(mNormalTexture, GL_RGB16F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid reconstructed normal RGB16F");
		configureTexture(mRawThicknessTexture, GL_R16F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid raw thickness R16F");
		configureTexture(mSmoothThicknessTextures[0], GL_R16F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid smooth thickness ping R16F");
		configureTexture(mSmoothThicknessTextures[1], GL_R16F, mWidth, mHeight, GL_NEAREST,
		                 "Fluid smooth thickness pong R16F");

		glGenFramebuffers(1, &mSceneFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mSceneFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       mSceneColourTexture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
		                       mSceneDepthTexture, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		verifyFramebuffer(mSceneFramebuffer, "Fluid scene framebuffer");
		utils::opengl::debug::nameObject(
			GL_FRAMEBUFFER, mSceneFramebuffer,
			"Fluid scene framebuffer " + std::to_string(mWidth) + "x" +
			std::to_string(mHeight));

		glGenFramebuffers(1, &mRawDepthFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mRawDepthFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       mRawDepthTexture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
		                       mRawHardwareDepthTexture, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		verifyFramebuffer(mRawDepthFramebuffer, "Fluid raw depth framebuffer");
		utils::opengl::debug::nameObject(
			GL_FRAMEBUFFER, mRawDepthFramebuffer,
			"Fluid raw depth framebuffer " + std::to_string(mWidth) + "x" +
			std::to_string(mHeight));

		glGenFramebuffers(1, &mRawThicknessFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mRawThicknessFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       mRawThicknessTexture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
		                       mRawHardwareDepthTexture, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		verifyFramebuffer(mRawThicknessFramebuffer, "Fluid raw thickness framebuffer");
		utils::opengl::debug::nameObject(
			GL_FRAMEBUFFER, mRawThicknessFramebuffer,
			"Fluid raw thickness framebuffer " + std::to_string(mWidth) + "x" +
			std::to_string(mHeight));

		glGenFramebuffers(1, &mFilterFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mFilterFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       mSmoothDepthTextures[0], 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		verifyFramebuffer(mFilterFramebuffer, "Fluid bilateral filter framebuffer");
		utils::opengl::debug::nameObject(
			GL_FRAMEBUFFER, mFilterFramebuffer,
			"Fluid bilateral filter framebuffer " + std::to_string(mWidth) + "x" +
			std::to_string(mHeight));
		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
		mFramebuffersComplete = true;
	}

	void ScreenSpaceFluidRenderer::verifyFramebuffer(GLuint const framebuffer,
	                                                 char const* const label)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			throw std::runtime_error(std::string(label) + " is incomplete (status " +
			                         std::to_string(status) + ").");
	}

	void ScreenSpaceFluidRenderer::recordStage(std::size_t const index)
	{
		if (index >= 8u) throw std::out_of_range("Fluid timestamp stage index is invalid.");
		if (index < mNextTimestampIndex) return;
		while (mNextTimestampIndex <= index) {
			if (mActiveTimingSlot != nullptr)
				glQueryCounter(mActiveTimingSlot->timestamps[mNextTimestampIndex],
				               GL_TIMESTAMP);
			++mNextTimestampIndex;
		}
	}

	ScreenSpaceFluidRenderer::TimingQuerySlot* ScreenSpaceFluidRenderer::acquireTimingSlot()
	{
		for (std::size_t offset = 0u; offset < mTimingSlots.size(); ++offset) {
			std::size_t const index = (mNextTimingSlot + offset) % mTimingSlots.size();
			if (!mTimingSlots[index].pending) {
				mNextTimingSlot = (index + 1u) % mTimingSlots.size();
				return &mTimingSlots[index];
			}
		}
		return nullptr;
	}

	void ScreenSpaceFluidRenderer::pollTimingQueries()
	{
		for (TimingQuerySlot& slot : mTimingSlots) {
			if (!slot.pending) continue;
			GLint available = GL_FALSE;
			glGetQueryObjectiv(slot.timestamps[7], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available != GL_TRUE) continue;
			std::array<GLuint64, 8u> timestamps{};
			for (std::size_t index = 0u; index < timestamps.size(); ++index)
				glGetQueryObjectui64v(slot.timestamps[index], GL_QUERY_RESULT,
				                      &timestamps[index]);
			auto const milliseconds = [&](std::size_t const begin, std::size_t const end) {
				return static_cast<double>(timestamps[end] - timestamps[begin]) / 1000000.0;
			};
			mTimings.sceneMilliseconds = milliseconds(0u, 1u);
			mTimings.rawDepthMilliseconds = milliseconds(1u, 2u);
			mTimings.rawThicknessMilliseconds = milliseconds(2u, 3u);
			mTimings.smoothDepthMilliseconds = milliseconds(3u, 4u);
			mTimings.smoothThicknessMilliseconds = milliseconds(4u, 5u);
			mTimings.reconstructNormalMilliseconds = milliseconds(5u, 6u);
			mTimings.presentMilliseconds = milliseconds(6u, 7u);
			mTimings.wholeRenderMilliseconds = milliseconds(0u, 7u);
			appendPathSample(slot.mode, mTimings.sceneMilliseconds,
			                 mTimings.wholeRenderMilliseconds);
			slot.pending = false;
		}
	}

	void ScreenSpaceFluidRenderer::appendPathSample(FluidDisplayMode const mode,
	                                                double const scene_ms,
	                                                double const whole_ms)
	{
		std::size_t const path_index = mode == FluidDisplayMode::Points ? 0u : 1u;
		PathHistory& history = mPathHistories[path_index];
		history.sceneSamples[history.nextSample] = scene_ms;
		history.wholeSamples[history.nextSample] = whole_ms;
		history.nextSample = (history.nextSample + 1u) % history.sceneSamples.size();
		history.sampleCount = std::min(history.sampleCount + 1u,
		                               history.sceneSamples.size());
		history.framebufferWidth = mWidth;
		history.framebufferHeight = mHeight;
	}

	double ScreenSpaceFluidRenderer::percentile(
		std::array<double, 240u> const& samples,
		std::size_t const count,
		double const fraction)
	{
		if (count == 0u) return 0.0;
		std::vector<double> sorted(samples.begin(), samples.begin() + count);
		std::sort(sorted.begin(), sorted.end());
		std::size_t const index = static_cast<std::size_t>(
			std::ceil(fraction * static_cast<double>(count))) - 1u;
		return sorted[std::min(index, sorted.size() - 1u)];
	}
}

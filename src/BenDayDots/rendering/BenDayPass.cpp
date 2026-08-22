#include "rendering/BenDayPass.hpp"

#include "core/helpers.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	struct BenDayShaderLocations
	{
		GLint sceneColor{ -1 };
		GLint sceneDepth{ -1 };
		GLint sceneNormal{ -1 };
		GLint framebufferSize{ -1 };
		GLint inverseFramebufferSize{ -1 };
		GLint cameraNear{ -1 };
		GLint cameraFar{ -1 };
		GLint effectEnabled{ -1 };
		GLint colorMode{ -1 };
		GLint cellSizePixels{ -1 };
		GLint inverseCellSizePixels{ -1 };
		GLint channelDotScales{ -1 };
		GLint channelRotations{ -1 };
		GLint registrationOffsets{ -1 };
		GLint exposureScale{ -1 };
		GLint toneContrast{ -1 };
		GLint inverseToneGamma{ -1 };
		GLint toneIntensity{ -1 };
		GLint posterizationEnabled{ -1 };
		GLint posterizationLevels{ -1 };
		GLint outlinesEnabled{ -1 };
		GLint outlineStrength{ -1 };
		GLint depthOutlineThreshold{ -1 };
		GLint normalOutlineThreshold{ -1 };
		GLint normalOutlineWeight{ -1 };
		GLint outlineThicknessPixels{ -1 };
		GLint surfaceTextureEnabled{ -1 };
		GLint paperGrainStrength{ -1 };
		GLint inversePaperGrainScalePixels{ -1 };
		GLint inkVariationStrength{ -1 };
	};

	GLuint cached_program{ 0u };
	BenDayShaderLocations cached_locations;

	void cacheShaderLocations(GLuint const program)
	{
		if (program == cached_program)
			return;

		cached_locations.sceneColor = glGetUniformLocation(program, "scene_color");
		cached_locations.sceneDepth = glGetUniformLocation(program, "scene_depth");
		cached_locations.sceneNormal = glGetUniformLocation(program, "scene_normal");
		cached_locations.framebufferSize = glGetUniformLocation(program, "framebuffer_size");
		cached_locations.inverseFramebufferSize =
			glGetUniformLocation(program, "inverse_framebuffer_size");
		cached_locations.cameraNear = glGetUniformLocation(program, "camera_near");
		cached_locations.cameraFar = glGetUniformLocation(program, "camera_far");
		cached_locations.effectEnabled = glGetUniformLocation(program, "effect_enabled");
		cached_locations.colorMode = glGetUniformLocation(program, "color_mode");
		cached_locations.cellSizePixels = glGetUniformLocation(program, "cell_size_pixels");
		cached_locations.inverseCellSizePixels =
			glGetUniformLocation(program, "inverse_cell_size_pixels");
		cached_locations.channelDotScales =
			glGetUniformLocation(program, "channel_dot_scales");
		cached_locations.channelRotations =
			glGetUniformLocation(program, "channel_rotations[0]");
		cached_locations.registrationOffsets =
			glGetUniformLocation(program, "registration_offsets[0]");
		cached_locations.exposureScale = glGetUniformLocation(program, "exposure_scale");
		cached_locations.toneContrast = glGetUniformLocation(program, "tone_contrast");
		cached_locations.inverseToneGamma =
			glGetUniformLocation(program, "inverse_tone_gamma");
		cached_locations.toneIntensity = glGetUniformLocation(program, "tone_intensity");
		cached_locations.posterizationEnabled =
			glGetUniformLocation(program, "posterization_enabled");
		cached_locations.posterizationLevels =
			glGetUniformLocation(program, "posterization_levels");
		cached_locations.outlinesEnabled = glGetUniformLocation(program, "outlines_enabled");
		cached_locations.outlineStrength = glGetUniformLocation(program, "outline_strength");
		cached_locations.depthOutlineThreshold =
			glGetUniformLocation(program, "depth_outline_threshold");
		cached_locations.normalOutlineThreshold =
			glGetUniformLocation(program, "normal_outline_threshold");
		cached_locations.normalOutlineWeight =
			glGetUniformLocation(program, "normal_outline_weight");
		cached_locations.outlineThicknessPixels =
			glGetUniformLocation(program, "outline_thickness_pixels");
		cached_locations.surfaceTextureEnabled =
			glGetUniformLocation(program, "surface_texture_enabled");
		cached_locations.paperGrainStrength =
			glGetUniformLocation(program, "paper_grain_strength");
		cached_locations.inversePaperGrainScalePixels =
			glGetUniformLocation(program, "inverse_paper_grain_scale_pixels");
		cached_locations.inkVariationStrength =
			glGetUniformLocation(program, "ink_variation_strength");
		cached_program = program;
	}

	void clearShaderLocations()
	{
		cached_program = 0u;
		cached_locations = {};
	}
}

namespace benday
{
	BenDayPass::~BenDayPass()
	{
		shutdown();
	}

	void BenDayPass::initialize()
	{
		shutdown();
		glGenSamplers(1, &mSceneSampler);
		glSamplerParameteri(mSceneSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(mSceneSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(mSceneSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(mSceneSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenSamplers(1, &mDepthSampler);
		glSamplerParameteri(mDepthSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glSamplerParameteri(mDepthSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(mDepthSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(mDepthSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	void BenDayPass::shutdown()
	{
		if (mSceneSampler != 0u)
			glDeleteSamplers(1, &mSceneSampler);
		if (mDepthSampler != 0u)
			glDeleteSamplers(1, &mDepthSampler);
		mSceneSampler = 0u;
		mDepthSampler = 0u;
		clearShaderLocations();
	}

	void BenDayPass::invalidateShaderLocations()
	{
		clearShaderLocations();
	}

	void BenDayPass::render(GLuint const program, GLuint const scene_color,
	                        GLuint const scene_depth, GLuint const scene_normal,
	                        int const width, int const height, float const camera_near,
	                        float const camera_far,
	                        BenDayParameters const& parameters) const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		glViewport(0, 0, width, height);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		glUseProgram(program);
		bool const program_changed = program != cached_program;
		cacheShaderLocations(program);
		if (program_changed) {
			glUniform1i(cached_locations.sceneColor, 0);
			glUniform1i(cached_locations.sceneDepth, 1);
			glUniform1i(cached_locations.sceneNormal, 2);
		}

		float const framebuffer_width = static_cast<float>(width);
		float const framebuffer_height = static_cast<float>(height);
		float const cell_size = std::max(parameters.cellSizePixels, 1.0f);
		float const gamma = std::max(parameters.gamma, 0.001f);
		float const paper_grain_scale =
			std::max(parameters.paperGrainScalePixels, 1.0f);
		std::array<float, 12u> channel_rotations;
		constexpr float degrees_to_radians = 0.017453292519943295f;
		for (std::size_t channel = 0u; channel < 3u; ++channel) {
			float const angle = parameters.anglesDegrees[channel] * degrees_to_radians;
			float const cosine = std::cos(angle);
			float const sine = std::sin(angle);
			std::size_t const offset = 4u * channel;
			channel_rotations[offset + 0u] = cosine;
			channel_rotations[offset + 1u] = sine;
			channel_rotations[offset + 2u] = -sine;
			channel_rotations[offset + 3u] = cosine;
		}

		glUniform2f(cached_locations.framebufferSize,
		            framebuffer_width, framebuffer_height);
		glUniform2f(cached_locations.inverseFramebufferSize,
		            1.0f / framebuffer_width, 1.0f / framebuffer_height);
		glUniform1f(cached_locations.cameraNear, camera_near);
		glUniform1f(cached_locations.cameraFar, camera_far);
		glUniform1i(cached_locations.effectEnabled, parameters.enabled ? 1 : 0);
		glUniform1i(cached_locations.colorMode, static_cast<GLint>(parameters.colorMode));
		glUniform1f(cached_locations.cellSizePixels, cell_size);
		glUniform1f(cached_locations.inverseCellSizePixels, 1.0f / cell_size);
		glUniform3fv(cached_locations.channelDotScales,
		             1, glm::value_ptr(parameters.channelDotScales));
		glUniformMatrix2fv(cached_locations.channelRotations, 3, GL_FALSE,
		                   channel_rotations.data());
		glUniform2fv(cached_locations.registrationOffsets,
		             static_cast<GLsizei>(parameters.registrationOffsets.size()),
		             glm::value_ptr(parameters.registrationOffsets.front()));
		glUniform1f(cached_locations.exposureScale, std::exp2(parameters.exposureStops));
		glUniform1f(cached_locations.toneContrast, parameters.contrast);
		glUniform1f(cached_locations.inverseToneGamma, 1.0f / gamma);
		glUniform1f(cached_locations.toneIntensity, parameters.intensity);
		glUniform1i(cached_locations.posterizationEnabled,
		            parameters.posterizationEnabled ? 1 : 0);
		glUniform1i(cached_locations.posterizationLevels,
		            parameters.posterizationLevels);
		glUniform1i(cached_locations.outlinesEnabled,
		            parameters.outlinesEnabled ? 1 : 0);
		glUniform1f(cached_locations.outlineStrength, parameters.outlineStrength);
		glUniform1f(cached_locations.depthOutlineThreshold,
		            parameters.depthOutlineThreshold);
		glUniform1f(cached_locations.normalOutlineThreshold,
		            parameters.normalOutlineThreshold);
		glUniform1f(cached_locations.normalOutlineWeight,
		            parameters.normalOutlineWeight);
		glUniform1f(cached_locations.outlineThicknessPixels,
		            parameters.outlineThicknessPixels);
		glUniform1i(cached_locations.surfaceTextureEnabled,
		            parameters.surfaceTextureEnabled ? 1 : 0);
		glUniform1f(cached_locations.paperGrainStrength,
		            parameters.paperGrainStrength);
		glUniform1f(cached_locations.inversePaperGrainScalePixels,
		            1.0f / paper_grain_scale);
		glUniform1f(cached_locations.inkVariationStrength,
		            parameters.inkVariationStrength);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, scene_color);
		glBindSampler(0u, mSceneSampler);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, scene_depth);
		glBindSampler(1u, mDepthSampler);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, scene_normal);
		glBindSampler(2u, mDepthSampler);
		bonobo::drawFullscreen();

		glUseProgram(0u);
		glBindSampler(2u, 0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1u, 0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0u, 0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
	}
}

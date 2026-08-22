#pragma once

#include <glad/glad.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>

namespace benday
{
	enum class BenDayColorMode : int
	{
		Monochrome = 0,
		RgbBlack,
		CmyWhite
	};

	struct BenDayParameters
	{
		bool enabled{ true };
		BenDayColorMode colorMode{ BenDayColorMode::RgbBlack };
		float cellSizePixels{ 14.0f };
		glm::vec3 channelDotScales{ 1.0f };
		glm::vec3 anglesDegrees{ 15.0f, 45.0f, 75.0f };
		std::array<glm::vec2, 3u> registrationOffsets{
			glm::vec2(-0.75f, 0.25f),
			glm::vec2(0.50f, -0.25f),
			glm::vec2(0.25f, 0.75f)
		};
		float exposureStops{ 0.0f };
		float contrast{ 1.0f };
		float gamma{ 1.0f };
		float intensity{ 1.0f };
		bool posterizationEnabled{ false };
		int posterizationLevels{ 4 };
		bool outlinesEnabled{ false };
		float outlineStrength{ 0.75f };
		float depthOutlineThreshold{ 0.08f };
		float normalOutlineThreshold{ 0.35f };
		float normalOutlineWeight{ 1.0f };
		float outlineThicknessPixels{ 1.0f };
		bool surfaceTextureEnabled{ false };
		float paperGrainStrength{ 0.08f };
		float paperGrainScalePixels{ 4.0f };
		float inkVariationStrength{ 0.08f };
	};

	class BenDayPass
	{
	public:
		BenDayPass() = default;
		~BenDayPass();

		BenDayPass(BenDayPass const&) = delete;
		BenDayPass& operator=(BenDayPass const&) = delete;

		void initialize();
		void shutdown();
		void invalidateShaderLocations();
		void render(GLuint program, GLuint scene_color, GLuint scene_depth,
		            GLuint scene_normal, int width, int height,
		            float camera_near, float camera_far,
		            BenDayParameters const& parameters) const;

	private:
		GLuint mSceneSampler{ 0u };
		GLuint mDepthSampler{ 0u };
	};
}

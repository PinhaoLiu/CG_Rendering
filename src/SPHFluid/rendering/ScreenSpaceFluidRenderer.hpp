#pragma once

#include <glad/glad.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace sph
{
	enum class FluidDisplayMode : int
	{
		Points = 0,
		ScreenSpaceDepth = 1,
		RawDepth = 2,
		RawThickness = 3,
		SmoothDepth = 4,
		SmoothThickness = 5,
		ViewNormal = 6,
		WorldNormal = 7,
		Reflection = 8,
		Refraction = 9,
		Transmission = 10
	};

	enum class NormalDepthSource : int
	{
		Raw = 0,
		Smoothed = 1
	};

	enum class NormalOutputSpace : int
	{
		View = 0,
		World = 1
	};

	struct ScreenSpaceFluidPrograms
	{
		GLuint rawDepth{ 0u };
		GLuint rawThickness{ 0u };
		GLuint bilateralFilter{ 0u };
		GLuint reconstructNormal{ 0u };
		GLuint present{ 0u };
	};

	struct ScreenSpaceRenderTimings
	{
		double sceneMilliseconds{ 0.0 };
		double rawDepthMilliseconds{ 0.0 };
		double rawThicknessMilliseconds{ 0.0 };
		double smoothDepthMilliseconds{ 0.0 };
		double smoothThicknessMilliseconds{ 0.0 };
		double reconstructNormalMilliseconds{ 0.0 };
		double presentMilliseconds{ 0.0 };
		double wholeRenderMilliseconds{ 0.0 };
	};

	struct ThicknessStats
	{
		float meanMetres{ 0.0f };
		float maximumMetres{ 0.0f };
		std::size_t coveredPixelCount{ 0u };
		std::size_t saturatedPixelCount{ 0u };
		std::size_t invalidPixelCount{ 0u };
		bool smoothed{ false };
		bool fresh{ false };
	};

	struct BilateralSmoothingParameters
	{
		float worldRadiusMetres{ 0.12f };
		float spatialSigmaFactor{ 0.5f };
		float depthFalloffPerMetre{ 40.0f };
		int maximumRadiusPixels{ 16 };
		int iterations{ 2 };
	};

	struct FluidMaterialParameters
	{
		float indexOfRefraction{ 1.33f };
		glm::vec3 absorptionPerMetre{ 0.35f, 0.08f, 0.025f };
		glm::vec3 scatteringColour{ 0.02f, 0.22f, 0.32f };
		float refractionScale{ 0.035f };
		float roughness{ 0.08f };
		bool reflectionEnabled{ true };
		bool refractionEnabled{ true };
		bool absorptionEnabled{ true };
	};

	struct RawDepthStats
	{
		float nearestMetres{ 0.0f };
		float farthestMetres{ 0.0f };
		std::size_t coveredPixelCount{ 0u };
		std::size_t invalidPixelCount{ 0u };
		bool fresh{ false };
	};

	struct RenderPathStats
	{
		std::size_t sampleCount{ 0u };
		double sceneMedianMilliseconds{ 0.0 };
		double sceneP95Milliseconds{ 0.0 };
		double wholeMedianMilliseconds{ 0.0 };
		double wholeP95Milliseconds{ 0.0 };
		int framebufferWidth{ 0 };
		int framebufferHeight{ 0 };
	};

	class ScreenSpaceFluidRenderer
	{
	public:
		ScreenSpaceFluidRenderer() = default;
		~ScreenSpaceFluidRenderer();

		ScreenSpaceFluidRenderer(ScreenSpaceFluidRenderer const&) = delete;
		ScreenSpaceFluidRenderer& operator=(ScreenSpaceFluidRenderer const&) = delete;

		void initialize(ScreenSpaceFluidPrograms const& programs, int width, int height);
		void setPrograms(ScreenSpaceFluidPrograms const& programs);
		void shutdown() noexcept;
		void resize(int width, int height);

		void beginScene(int width, int height, glm::vec3 const& clear_colour);
		void endScene();
		void renderRawDepth(GLuint position_buffer,
		                    std::size_t particle_count,
		                    float particle_radius,
		                    glm::mat4 const& world_to_view,
		                    glm::mat4 const& view_to_clip,
		                    glm::mat4 const& clip_to_view);
		void renderRawThickness(GLuint position_buffer,
		                        std::size_t particle_count,
		                        float particle_radius,
		                        float thickness_scale,
		                        glm::mat4 const& world_to_view,
		                        glm::mat4 const& view_to_clip,
		                        glm::mat4 const& clip_to_view);
		void smoothDepth(BilateralSmoothingParameters const& parameters,
		                 glm::mat4 const& view_to_clip);
		void smoothThickness(BilateralSmoothingParameters const& parameters,
		                     glm::mat4 const& view_to_clip);
		void reconstructNormals(NormalDepthSource depth_source,
		                        NormalOutputSpace output_space,
		                        glm::mat4 const& clip_to_view,
		                        glm::mat4 const& view_to_world);
		void present(FluidDisplayMode mode, float debug_depth_scale,
		             float debug_thickness_scale = 1.0f,
		             FluidMaterialParameters const& material = {},
		             glm::mat4 const& clip_to_view = glm::mat4(1.0f),
		             glm::mat4 const& view_to_clip = glm::mat4(1.0f),
		             glm::mat4 const& view_to_world = glm::mat4(1.0f));

		int width() const noexcept;
		int height() const noexcept;
		bool framebuffersComplete() const noexcept;
		std::size_t reservedTextureBytes() const noexcept;
		GLuint sceneFramebuffer() const noexcept;
		GLuint sceneColourTexture() const noexcept;
		GLuint sceneDepthTexture() const noexcept;
		GLuint rawDepthTexture() const noexcept;
		GLuint smoothDepthTexture(std::size_t index) const;
		GLuint normalTexture() const noexcept;
		GLuint rawThicknessTexture() const noexcept;
		GLuint smoothThicknessTexture(std::size_t index) const;
		ScreenSpaceRenderTimings const& timings() const noexcept;
		RawDepthStats const& rawDepthStats() const noexcept;
		ThicknessStats const& thicknessStats() const noexcept;
		RenderPathStats renderPathStats(FluidDisplayMode mode) const;
		void refreshRawDepthStats();
		void refreshThicknessStats(bool smoothed);
		void synchronizeTimings();

	private:
		struct TimingQuerySlot
		{
			std::array<GLuint, 8u> timestamps{};
			bool pending{ false };
			FluidDisplayMode mode{ FluidDisplayMode::Points };
		};

		struct PathHistory
		{
			std::array<double, 240u> sceneSamples{};
			std::array<double, 240u> wholeSamples{};
			std::size_t sampleCount{ 0u };
			std::size_t nextSample{ 0u };
			int framebufferWidth{ 0 };
			int framebufferHeight{ 0 };
		};

		static constexpr float kDepthSentinel = 1.0e20f;
		static constexpr std::size_t kTimingSlotCount = 8u;

		void requireInitialized() const;
		void destroyTargets() noexcept;
		void createTargets();
		void verifyFramebuffer(GLuint framebuffer, char const* label);
		void recordStage(std::size_t index);
		void filterScalarField(GLuint source_texture, GLuint guide_depth_texture,
		                       std::array<GLuint, 2u> const& targets,
		                       bool depth_field,
		                       BilateralSmoothingParameters const& parameters,
		                       glm::mat4 const& view_to_clip,
		                       char const* debug_label);
		TimingQuerySlot* acquireTimingSlot();
		void pollTimingQueries();
		void appendPathSample(FluidDisplayMode mode, double scene_ms, double whole_ms);
		static double percentile(std::array<double, 240u> const& samples,
		                         std::size_t count, double fraction);

		ScreenSpaceFluidPrograms mPrograms;
		GLuint mSceneFramebuffer{ 0u };
		GLuint mRawDepthFramebuffer{ 0u };
		GLuint mRawThicknessFramebuffer{ 0u };
		GLuint mFilterFramebuffer{ 0u };
		GLuint mSceneColourTexture{ 0u };
		GLuint mSceneDepthTexture{ 0u };
		GLuint mRawDepthTexture{ 0u };
		GLuint mRawHardwareDepthTexture{ 0u };
		std::array<GLuint, 2u> mSmoothDepthTextures{};
		GLuint mNormalTexture{ 0u };
		GLuint mRawThicknessTexture{ 0u };
		std::array<GLuint, 2u> mSmoothThicknessTextures{};
		GLuint mParticleVertexArray{ 0u };
		GLuint mFullscreenVertexArray{ 0u };
		int mWidth{ 0 };
		int mHeight{ 0 };
		bool mFramebuffersComplete{ false };
		bool mInitialized{ false };
		bool mSceneActive{ false };
		std::size_t mNextTimestampIndex{ 0u };
		std::array<TimingQuerySlot, kTimingSlotCount> mTimingSlots{};
		std::size_t mNextTimingSlot{ 0u };
		TimingQuerySlot* mActiveTimingSlot{ nullptr };
		ScreenSpaceRenderTimings mTimings;
		RawDepthStats mRawDepthStats;
		ThicknessStats mThicknessStats;
		std::array<PathHistory, 2u> mPathHistories{};
	};
}

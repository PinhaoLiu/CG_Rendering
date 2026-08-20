#pragma once

#include "simulation/ISphSolver.hpp"
#include "simulation/GpuSphTypes.hpp"
#include "simulation/SphTypes.hpp"

#include <glad/glad.h>

#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	class GpuBruteForceSphSolver final : public ISphSolver
	{
	public:
		GpuBruteForceSphSolver(std::size_t maximum_particle_count,
		                       SphParameters const& parameters = SphParameters{});
		~GpuBruteForceSphSolver();

		GpuBruteForceSphSolver(GpuBruteForceSphSolver const&) = delete;
		GpuBruteForceSphSolver& operator=(GpuBruteForceSphSolver const&) = delete;

		static bool isSupported() noexcept;
		void initialize(GLuint density_program,
		                GLuint force_program,
		                GLuint integrate_program);
		void setPrograms(GLuint density_program,
		                 GLuint force_program,
		                 GLuint integrate_program);
		void shutdown() noexcept;

		void reset() override;
		std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) override;
		void setBoundary(BoxBoundary const& boundary) override;
		void step(float fixed_delta_seconds) override;
		void stepProfiled(float fixed_delta_seconds,
		                  std::array<GLuint, 4u> const& timestamp_queries);
		std::vector<glm::vec3> const& positions() const noexcept override;
		std::size_t particleCount() const noexcept override;
		std::size_t capacity() const noexcept override;
		char const* backendName() const noexcept override;

		float particleRadius() const noexcept;
		SphParameters const& parameters() const noexcept;
		void setParameters(SphParameters const& parameters);
		SphDiagnostics const& diagnostics() const noexcept;
		SphStageTimings const& stageTimings() const noexcept;
		ParticleStorageStats storageStats() const noexcept;
		bool diagnosticsFresh() const noexcept;
		std::uint64_t readbackCount() const noexcept;
		GLuint positionBuffer() const noexcept;

		GpuParticleState readbackState();
		void refreshDiagnostics();
		void synchronizeTimings();

	private:
		struct TimingQuerySlot
		{
			std::array<GLuint, 4u> timestamps{};
			bool pending{ false };
		};

		static constexpr GLuint kLocalSize = 128u;
		static constexpr std::size_t kTimingSlotCount = 8u;

		static void validateParameters(SphParameters const& parameters);
		void requireInitialized() const;
		void createBuffers();
		void bindParticleBuffers() const;
		void setDensityUniforms() const;
		void setForceUniforms() const;
		void setIntegrateUniforms(float fixed_delta_seconds) const;
		void dispatch(GLuint program) const;
		void stepInternal(float fixed_delta_seconds,
		                  std::array<GLuint, 4u> const* timestamp_queries);
		TimingQuerySlot* acquireTimingSlot();
		void pollTimingQueries();
		void rebuildDiagnostics(GpuParticleState const& state);

		std::size_t mMaximumParticleCount;
		std::size_t mParticleCount{ 0u };
		SphParameters mParameters;
		BoxBoundary mBoundary;
		GLuint mDensityProgram{ 0u };
		GLuint mForceProgram{ 0u };
		GLuint mIntegrateProgram{ 0u };
		GLuint mPositionBuffer{ 0u };
		GLuint mVelocityBuffer{ 0u };
		GLuint mAccelerationBuffer{ 0u };
		GLuint mDensityBuffer{ 0u };
		GLuint mPressureBuffer{ 0u };
		std::array<TimingQuerySlot, kTimingSlotCount> mTimingSlots{};
		std::size_t mNextTimingSlot{ 0u };
		SphDiagnostics mDiagnostics;
		SphStageTimings mStageTimings;
		std::vector<glm::vec3> mPositionSnapshot;
		std::vector<glm::vec4> mUploadPositions;
		std::vector<glm::vec4> mUploadVelocities;
		std::vector<glm::vec4> mUploadAccelerations;
		std::vector<float> mUploadDensities;
		std::vector<float> mUploadPressures;
		std::uint64_t mReadbackCount{ 0u };
		bool mInitialized{ false };
		bool mDiagnosticsFresh{ false };
	};
}

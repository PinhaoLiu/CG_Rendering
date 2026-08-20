#pragma once

#include "simulation/GpuSphTypes.hpp"
#include "simulation/ISphSolver.hpp"
#include "simulation/SphTypes.hpp"

#include <glad/glad.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sph
{
	struct GpuGridPrograms
	{
		GLuint count{ 0u };
		GLuint scanBlocks{ 0u };
		GLuint scanBlockSums{ 0u };
		GLuint addBlockOffsets{ 0u };
		GLuint prepareScatter{ 0u };
		GLuint scatter{ 0u };
		GLuint densityPressure{ 0u };
		GLuint force{ 0u };
		GLuint integrate{ 0u };
	};

	struct GpuGridStats
	{
		glm::uvec3 dimensions{ 0u };
		float cellSize{ 0.0f };
		std::uint32_t cellCount{ 0u };
		std::uint32_t blockCount{ 0u };
		std::uint32_t nonEmptyCellCount{ 0u };
		std::uint32_t maximumCellOccupancy{ 0u };
		std::array<std::uint32_t, 4u> errors{};
	};

	struct GpuGridSnapshot
	{
		GpuGridStats stats;
		std::vector<std::uint32_t> cellCounts;
		std::vector<std::uint32_t> cellOffsets;
		std::vector<std::uint32_t> particleIndices;
	};

	class GpuGridSphSolver final : public ISphSolver
	{
	public:
		GpuGridSphSolver(std::size_t maximum_particle_count,
		                 SphParameters const& parameters = SphParameters{});
		~GpuGridSphSolver();

		GpuGridSphSolver(GpuGridSphSolver const&) = delete;
		GpuGridSphSolver& operator=(GpuGridSphSolver const&) = delete;

		static bool isSupported() noexcept;
		void initialize(GpuGridPrograms const& programs);
		void setPrograms(GpuGridPrograms const& programs);
		void shutdown() noexcept;

		void reset() override;
		std::size_t spawnParticles(std::vector<ParticleSpawn> const& particles) override;
		void setBoundary(BoxBoundary const& boundary) override;
		void step(float fixed_delta_seconds) override;
		void stepProfiled(float fixed_delta_seconds,
		                  std::array<GLuint, 5u> const& timestamp_queries);
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
		GpuGridStats const& gridStats() const noexcept;
		std::size_t gridReservedBytes() const noexcept;
		bool diagnosticsFresh() const noexcept;
		std::uint64_t readbackCount() const noexcept;
		GLuint positionBuffer() const noexcept;

		GpuParticleState readbackState();
		GpuGridSnapshot readbackGrid();
		void refreshDiagnostics();
		void synchronizeTimings();

	private:
		struct TimingQuerySlot
		{
			std::array<GLuint, 5u> timestamps{};
			bool pending{ false };
		};

		static constexpr GLuint kParticleLocalSize = 128u;
		static constexpr GLuint kScanLocalSize = 256u;
		static constexpr std::uint32_t kMaximumCellsPerAxis = 128u;
		static constexpr std::size_t kTimingSlotCount = 8u;

		static void validateParameters(SphParameters const& parameters);
		void requireInitialized() const;
		void configureGrid();
		void createBuffers();
		void resizeGridBuffers();
		void bindBuffers() const;
		void setGridUniforms(GLuint program) const;
		void setDensityUniforms() const;
		void setForceUniforms() const;
		void setIntegrateUniforms(float fixed_delta_seconds) const;
		void dispatchParticles(GLuint program) const;
		void dispatchCells(GLuint program, GLuint local_size) const;
		void buildGrid();
		void stepInternal(float fixed_delta_seconds,
		                  std::array<GLuint, 5u> const* timestamp_queries);
		TimingQuerySlot* acquireTimingSlot();
		void pollTimingQueries();
		void rebuildDiagnostics(GpuParticleState const& state);

		std::size_t mMaximumParticleCount;
		std::size_t mParticleCount{ 0u };
		SphParameters mParameters;
		BoxBoundary mBoundary;
		GpuGridPrograms mPrograms;
		GpuGridStats mGridStats;
		glm::vec3 mGridMinimum{ 0.0f };

		GLuint mPositionBuffer{ 0u };
		GLuint mVelocityBuffer{ 0u };
		GLuint mAccelerationBuffer{ 0u };
		GLuint mDensityBuffer{ 0u };
		GLuint mPressureBuffer{ 0u };
		GLuint mCellCountBuffer{ 0u };
		GLuint mCellOffsetBuffer{ 0u };
		GLuint mParticleIndexBuffer{ 0u };
		GLuint mBlockSumBuffer{ 0u };
		GLuint mBlockOffsetBuffer{ 0u };
		GLuint mGridErrorBuffer{ 0u };
		GLuint mCellCursorBuffer{ 0u };

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

#include "simulation/GpuGridSphSolver.hpp"

#include "core/opengl.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sph
{
	namespace
	{
		constexpr GLuint kPositionBinding = 0u;
		constexpr GLuint kVelocityBinding = 1u;
		constexpr GLuint kAccelerationBinding = 2u;
		constexpr GLuint kDensityBinding = 3u;
		constexpr GLuint kPressureBinding = 4u;
		constexpr GLuint kCellCountBinding = 5u;
		constexpr GLuint kCellOffsetBinding = 6u;
		constexpr GLuint kParticleIndexBinding = 7u;
		constexpr GLuint kBlockSumBinding = 8u;
		constexpr GLuint kBlockOffsetBinding = 9u;
		constexpr GLuint kGridErrorBinding = 10u;
		constexpr GLuint kCellCursorBinding = 11u;

		void allocateBuffer(GLuint& buffer, GLsizeiptr const bytes, char const* label)
		{
			glGenBuffers(1, &buffer);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
			utils::opengl::debug::nameObject(GL_BUFFER, buffer, label);
		}

		void resizeBuffer(GLuint const buffer, GLsizeiptr const bytes)
		{
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
		}

		template <typename T>
		void uploadRange(GLuint const buffer, std::size_t const first,
		                 std::vector<T> const& values)
		{
			if (values.empty()) return;
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER,
			                static_cast<GLintptr>(first * sizeof(T)),
			                static_cast<GLsizeiptr>(values.size() * sizeof(T)), values.data());
		}

		template <typename T>
		std::vector<T> readBuffer(GLuint const buffer, std::size_t const count)
		{
			std::vector<T> values(count);
			if (count == 0u) return values;
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
			                   static_cast<GLsizeiptr>(count * sizeof(T)), values.data());
			return values;
		}

		void setUnsigned(GLuint p, char const* n, GLuint v) { glUniform1ui(glGetUniformLocation(p, n), v); }
		void setFloat(GLuint p, char const* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
		void setVec3(GLuint p, char const* n, glm::vec3 const& v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }
		void setUVec3(GLuint p, char const* n, glm::uvec3 const& v) { glUniform3ui(glGetUniformLocation(p, n), v.x, v.y, v.z); }
	}

	GpuGridSphSolver::GpuGridSphSolver(std::size_t const maximum_particle_count,
	                                     SphParameters const& parameters) :
		mMaximumParticleCount(maximum_particle_count), mParameters(parameters)
	{
		if (mMaximumParticleCount == 0u ||
		    mMaximumParticleCount > std::numeric_limits<GLuint>::max())
			throw std::invalid_argument("GPU particle capacity is outside the dispatch range.");
		validateParameters(mParameters);
		setBoundary(mBoundary);
		mPositionSnapshot.reserve(mMaximumParticleCount);
		mUploadPositions.reserve(256u);
		mUploadVelocities.reserve(256u);
		mUploadAccelerations.reserve(256u);
		mUploadDensities.reserve(256u);
		mUploadPressures.reserve(256u);
	}

	GpuGridSphSolver::~GpuGridSphSolver() { shutdown(); }

	bool GpuGridSphSolver::isSupported() noexcept { return GLAD_GL_VERSION_4_3; }

	void GpuGridSphSolver::initialize(GpuGridPrograms const& programs)
	{
		if (!isSupported())
			throw std::runtime_error("GPU SPH requires OpenGL 4.3 compute shaders and SSBOs.");
		shutdown();
		setPrograms(programs);
		GLint maximum_invocations = 0;
		glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maximum_invocations);
		if (maximum_invocations < static_cast<GLint>(kScanLocalSize))
			throw std::runtime_error("GPU compute work-group limit is below 256 invocations.");
		GLint64 maximum_ssbo_size = 0;
		glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maximum_ssbo_size);
		std::size_t const largest = std::max(
			mMaximumParticleCount * sizeof(glm::vec4),
			static_cast<std::size_t>(mGridStats.cellCount) * sizeof(std::uint32_t));
		if (maximum_ssbo_size < static_cast<GLint64>(largest))
			throw std::runtime_error("GPU SSBO size limit is below the requested particle capacity.");
		createBuffers();
		for (TimingQuerySlot& slot : mTimingSlots)
			glGenQueries(static_cast<GLsizei>(slot.timestamps.size()), slot.timestamps.data());
		mInitialized = true;
		reset();
	}

	void GpuGridSphSolver::setPrograms(GpuGridPrograms const& programs)
	{
		GLuint const values[] = { programs.count, programs.scanBlocks,
			programs.scanBlockSums, programs.addBlockOffsets, programs.prepareScatter,
			programs.scatter, programs.densityPressure, programs.force, programs.integrate };
		for (GLuint const value : values)
			if (value == 0u) throw std::invalid_argument("All GPU SPH compute programs must be valid.");
		mPrograms = programs;
	}

	void GpuGridSphSolver::shutdown() noexcept
	{
		if (mInitialized) {
			for (TimingQuerySlot& slot : mTimingSlots) {
				glDeleteQueries(static_cast<GLsizei>(slot.timestamps.size()), slot.timestamps.data());
				slot = {};
			}
			GLuint buffers[] = { mPositionBuffer, mVelocityBuffer, mAccelerationBuffer,
				mDensityBuffer, mPressureBuffer, mCellCountBuffer, mCellOffsetBuffer,
				mParticleIndexBuffer, mBlockSumBuffer, mBlockOffsetBuffer,
				mGridErrorBuffer, mCellCursorBuffer };
			glDeleteBuffers(12, buffers);
		}
		mPositionBuffer = mVelocityBuffer = mAccelerationBuffer = 0u;
		mDensityBuffer = mPressureBuffer = 0u;
		mCellCountBuffer = mCellOffsetBuffer = mParticleIndexBuffer = 0u;
		mBlockSumBuffer = mBlockOffsetBuffer = mGridErrorBuffer = mCellCursorBuffer = 0u;
		mParticleCount = 0u;
		mInitialized = false;
	}

	void GpuGridSphSolver::reset()
	{
		mParticleCount = 0u;
		mPositionSnapshot.clear();
		mDiagnostics = {};
		mStageTimings = {};
		mGridStats.nonEmptyCellCount = 0u;
		mGridStats.maximumCellOccupancy = 0u;
		mGridStats.errors = {};
		mDiagnosticsFresh = true;
		mReadbackCount = 0u;
	}

	std::size_t GpuGridSphSolver::spawnParticles(std::vector<ParticleSpawn> const& particles)
	{
		requireInitialized();
		std::size_t const accepted = std::min(mMaximumParticleCount - mParticleCount, particles.size());
		mUploadPositions.resize(accepted);
		mUploadVelocities.resize(accepted);
		mUploadAccelerations.assign(accepted, glm::vec4(0.0f));
		mUploadDensities.assign(accepted, mParameters.restDensity);
		mUploadPressures.assign(accepted, 0.0f);
		for (std::size_t i = 0u; i < accepted; ++i) {
			mUploadPositions[i] = glm::vec4(particles[i].position, 1.0f);
			mUploadVelocities[i] = glm::vec4(particles[i].velocity, 0.0f);
			mPositionSnapshot.push_back(particles[i].position);
		}
		uploadRange(mPositionBuffer, mParticleCount, mUploadPositions);
		uploadRange(mVelocityBuffer, mParticleCount, mUploadVelocities);
		uploadRange(mAccelerationBuffer, mParticleCount, mUploadAccelerations);
		uploadRange(mDensityBuffer, mParticleCount, mUploadDensities);
		uploadRange(mPressureBuffer, mParticleCount, mUploadPressures);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		mParticleCount += accepted;
		mDiagnosticsFresh = false;
		return accepted;
	}

	void GpuGridSphSolver::setBoundary(BoxBoundary const& boundary)
	{
		mBoundary = boundary;
		float const minimum_extent = 2.0f * mParameters.particleRadius;
		for (int axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(mBoundary.center[axis]) || !std::isfinite(mBoundary.halfExtent[axis]))
				throw std::invalid_argument("The collision boundary must be finite.");
			mBoundary.halfExtent[axis] = std::max(minimum_extent, mBoundary.halfExtent[axis]);
		}
		mBoundary.restitution = std::max(0.0f, std::min(1.0f, mBoundary.restitution));
		mBoundary.friction = std::max(0.0f, std::min(1.0f, mBoundary.friction));
		configureGrid();
	}

	void GpuGridSphSolver::step(float const dt) { stepInternal(dt, nullptr); }

	void GpuGridSphSolver::stepProfiled(float const dt,
	                                      std::array<GLuint, 5u> const& queries)
	{
		for (GLuint const query : queries)
			if (query == 0u) throw std::invalid_argument("Profile timestamp queries must be valid.");
		stepInternal(dt, &queries);
	}

	void GpuGridSphSolver::stepInternal(float const dt,
	                                    std::array<GLuint, 5u> const* queries)
	{
		requireInitialized();
		if (!(dt > 0.0f) || !std::isfinite(dt))
			throw std::invalid_argument("SPH fixed delta must be finite and positive.");
		pollTimingQueries();
		if (mParticleCount == 0u) { ++mDiagnostics.completedSteps; return; }
		bindBuffers();
		TimingQuerySlot* timing = queries == nullptr ? acquireTimingSlot() : nullptr;
		auto stamp = [&](std::size_t i) {
			GLuint q = queries ? (*queries)[i] : (timing ? timing->timestamps[i] : 0u);
			if (q != 0u) glQueryCounter(q, GL_TIMESTAMP);
		};
		stamp(0u);

		utils::opengl::debug::beginDebugGroup("SPH grid count-scan-scatter");
		buildGrid();
		utils::opengl::debug::endDebugGroup();
		stamp(1u);

		utils::opengl::debug::beginDebugGroup("SPH density-pressure");
		glUseProgram(mPrograms.densityPressure);
		setDensityUniforms();
		dispatchParticles(mPrograms.densityPressure);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		stamp(2u);

		utils::opengl::debug::beginDebugGroup("SPH force");
		glUseProgram(mPrograms.force);
		setForceUniforms();
		dispatchParticles(mPrograms.force);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		stamp(3u);

		utils::opengl::debug::beginDebugGroup("SPH integrate");
		glUseProgram(mPrograms.integrate);
		setIntegrateUniforms(dt);
		dispatchParticles(mPrograms.integrate);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		stamp(4u);
		if (timing) timing->pending = true;
		glUseProgram(0u);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		++mDiagnostics.completedSteps;
		mDiagnosticsFresh = false;
	}

	std::vector<glm::vec3> const& GpuGridSphSolver::positions() const noexcept { return mPositionSnapshot; }
	std::size_t GpuGridSphSolver::particleCount() const noexcept { return mParticleCount; }
	std::size_t GpuGridSphSolver::capacity() const noexcept { return mMaximumParticleCount; }
	char const* GpuGridSphSolver::backendName() const noexcept { return "GPU WCSPH Uniform Grid (OpenGL Compute)"; }
	float GpuGridSphSolver::particleRadius() const noexcept { return mParameters.particleRadius; }
	SphParameters const& GpuGridSphSolver::parameters() const noexcept { return mParameters; }

	void GpuGridSphSolver::setParameters(SphParameters const& parameters)
	{
		validateParameters(parameters);
		mParameters = parameters;
		setBoundary(mBoundary);
		mDiagnosticsFresh = false;
	}

	SphDiagnostics const& GpuGridSphSolver::diagnostics() const noexcept { return mDiagnostics; }
	SphStageTimings const& GpuGridSphSolver::stageTimings() const noexcept { return mStageTimings; }
	GpuGridStats const& GpuGridSphSolver::gridStats() const noexcept { return mGridStats; }

	ParticleStorageStats GpuGridSphSolver::storageStats() const noexcept
	{
		constexpr std::size_t bytes = 3u * sizeof(glm::vec4) + 2u * sizeof(float);
		return { mParticleCount, mMaximumParticleCount, bytes,
			mParticleCount * bytes, mMaximumParticleCount * bytes, false };
	}

	std::size_t GpuGridSphSolver::gridReservedBytes() const noexcept
	{
		return (4u * static_cast<std::size_t>(mGridStats.cellCount) +
		        2u * static_cast<std::size_t>(mGridStats.blockCount) +
		        mMaximumParticleCount + 4u) * sizeof(std::uint32_t);
	}

	bool GpuGridSphSolver::diagnosticsFresh() const noexcept { return mDiagnosticsFresh; }
	std::uint64_t GpuGridSphSolver::readbackCount() const noexcept { return mReadbackCount; }
	GLuint GpuGridSphSolver::positionBuffer() const noexcept { return mPositionBuffer; }

	GpuParticleState GpuGridSphSolver::readbackState()
	{
		requireInitialized();
		glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
		auto p = readBuffer<glm::vec4>(mPositionBuffer, mParticleCount);
		auto v = readBuffer<glm::vec4>(mVelocityBuffer, mParticleCount);
		auto a = readBuffer<glm::vec4>(mAccelerationBuffer, mParticleCount);
		GpuParticleState state;
		state.positions.resize(mParticleCount);
		state.velocities.resize(mParticleCount);
		state.accelerations.resize(mParticleCount);
		for (std::size_t i = 0u; i < mParticleCount; ++i) {
			state.positions[i] = glm::vec3(p[i]);
			state.velocities[i] = glm::vec3(v[i]);
			state.accelerations[i] = glm::vec3(a[i]);
		}
		state.densities = readBuffer<float>(mDensityBuffer, mParticleCount);
		state.pressures = readBuffer<float>(mPressureBuffer, mParticleCount);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		mPositionSnapshot = state.positions;
		++mReadbackCount;
		rebuildDiagnostics(state);
		mDiagnosticsFresh = true;
		return state;
	}

	GpuGridSnapshot GpuGridSphSolver::readbackGrid()
	{
		requireInitialized();
		glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
		GpuGridSnapshot snapshot;
		snapshot.stats = mGridStats;
		snapshot.cellCounts = readBuffer<std::uint32_t>(mCellCountBuffer, mGridStats.cellCount);
		snapshot.cellOffsets = readBuffer<std::uint32_t>(mCellOffsetBuffer, mGridStats.cellCount);
		snapshot.particleIndices = readBuffer<std::uint32_t>(mParticleIndexBuffer, mParticleCount);
		auto errors = readBuffer<std::uint32_t>(mGridErrorBuffer, 4u);
		std::copy(errors.begin(), errors.end(), snapshot.stats.errors.begin());
		snapshot.stats.nonEmptyCellCount = 0u;
		snapshot.stats.maximumCellOccupancy = 0u;
		for (std::uint32_t count : snapshot.cellCounts) {
			if (count != 0u) ++snapshot.stats.nonEmptyCellCount;
			snapshot.stats.maximumCellOccupancy = std::max(snapshot.stats.maximumCellOccupancy, count);
		}
		mGridStats = snapshot.stats;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		++mReadbackCount;
		return snapshot;
	}

	void GpuGridSphSolver::refreshDiagnostics() { (void)readbackState(); (void)readbackGrid(); }
	void GpuGridSphSolver::synchronizeTimings() { requireInitialized(); glFinish(); pollTimingQueries(); }

	void GpuGridSphSolver::validateParameters(SphParameters const& p)
	{
		auto positive = [](float v) { return v > 0.0f && std::isfinite(v); };
		if (!positive(p.smoothingRadius) || !positive(p.particleRadius) ||
		    p.smoothingRadius <= p.particleRadius || !positive(p.restDensity) ||
		    !positive(p.particleMass) || p.gasStiffness < 0.0f || !std::isfinite(p.gasStiffness) ||
		    p.viscosity < 0.0f || !std::isfinite(p.viscosity) ||
		    !std::isfinite(p.gravity.x) || !std::isfinite(p.gravity.y) || !std::isfinite(p.gravity.z))
			throw std::invalid_argument("Invalid WCSPH parameters.");
	}

	void GpuGridSphSolver::requireInitialized() const
	{
		if (!mInitialized) throw std::logic_error("GPU SPH solver must be initialized after creating a GL context.");
	}

	void GpuGridSphSolver::configureGrid()
	{
		glm::vec3 extent = 2.0f * mBoundary.halfExtent;
		if (!std::isfinite(extent.x) || !std::isfinite(extent.y) || !std::isfinite(extent.z))
			throw std::invalid_argument("The collision boundary extent is too large.");
		float largest = std::max(extent.x, std::max(extent.y, extent.z));
		float cell_size = std::max(mParameters.smoothingRadius,
		                           largest / static_cast<float>(kMaximumCellsPerAxis));
		glm::uvec3 dimensions(
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.x / cell_size))),
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.y / cell_size))),
			std::max(1u, static_cast<std::uint32_t>(std::ceil(extent.z / cell_size))));
		std::uint64_t count64 = static_cast<std::uint64_t>(dimensions.x) * dimensions.y * dimensions.z;
		if (count64 == 0u || count64 > std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("SPH grid cell count exceeds uint32_t.");
		std::uint32_t old_count = mGridStats.cellCount;
		mGridStats.dimensions = dimensions;
		mGridStats.cellSize = cell_size;
		mGridStats.cellCount = static_cast<std::uint32_t>(count64);
		mGridStats.blockCount = (mGridStats.cellCount + kScanLocalSize - 1u) / kScanLocalSize;
		mGridMinimum = mBoundary.center - mBoundary.halfExtent;
		if (mInitialized && old_count != mGridStats.cellCount)
			resizeGridBuffers();
	}

	void GpuGridSphSolver::createBuffers()
	{
		allocateBuffer(mPositionBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)), "SPH positions SSBO / vertex buffer");
		allocateBuffer(mVelocityBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)), "SPH velocities SSBO");
		allocateBuffer(mAccelerationBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)), "SPH accelerations SSBO");
		allocateBuffer(mDensityBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(float)), "SPH densities SSBO");
		allocateBuffer(mPressureBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(float)), "SPH pressures SSBO");
		allocateBuffer(mCellCountBuffer, 4, "SPH grid counts SSBO");
		allocateBuffer(mCellOffsetBuffer, 4, "SPH grid offsets SSBO");
		allocateBuffer(mParticleIndexBuffer, static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(std::uint32_t)), "SPH grid particle indices SSBO");
		allocateBuffer(mBlockSumBuffer, 4, "SPH grid block sums SSBO");
		allocateBuffer(mBlockOffsetBuffer, 4, "SPH grid block offsets SSBO");
		allocateBuffer(mGridErrorBuffer, 4 * sizeof(std::uint32_t), "SPH grid errors SSBO");
		allocateBuffer(mCellCursorBuffer, 4, "SPH grid scatter cursors SSBO");
		resizeGridBuffers();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
	}

	void GpuGridSphSolver::resizeGridBuffers()
	{
		GLsizeiptr cells = static_cast<GLsizeiptr>(mGridStats.cellCount * sizeof(std::uint32_t));
		GLsizeiptr blocks = static_cast<GLsizeiptr>(mGridStats.blockCount * sizeof(std::uint32_t));
		resizeBuffer(mCellCountBuffer, cells);
		resizeBuffer(mCellOffsetBuffer, cells);
		resizeBuffer(mCellCursorBuffer, cells);
		resizeBuffer(mBlockSumBuffer, blocks);
		resizeBuffer(mBlockOffsetBuffer, blocks);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
	}

	void GpuGridSphSolver::bindBuffers() const
	{
		GLuint const buffers[] = { mPositionBuffer, mVelocityBuffer, mAccelerationBuffer,
			mDensityBuffer, mPressureBuffer, mCellCountBuffer, mCellOffsetBuffer,
			mParticleIndexBuffer, mBlockSumBuffer, mBlockOffsetBuffer,
			mGridErrorBuffer, mCellCursorBuffer };
		for (GLuint binding = 0u; binding < 12u; ++binding)
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffers[binding]);
	}

	void GpuGridSphSolver::setGridUniforms(GLuint p) const
	{
		setUVec3(p, "uGridDimensions", mGridStats.dimensions);
		setVec3(p, "uGridMinimum", mGridMinimum);
		setFloat(p, "uGridCellSize", mGridStats.cellSize);
	}

	void GpuGridSphSolver::setDensityUniforms() const
	{
		GLuint p = mPrograms.densityPressure;
		setUnsigned(p, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setGridUniforms(p);
		setFloat(p, "uSmoothingRadius", mParameters.smoothingRadius);
		setFloat(p, "uParticleMass", mParameters.particleMass);
		setFloat(p, "uRestDensity", mParameters.restDensity);
		setFloat(p, "uGasStiffness", mParameters.gasStiffness);
	}

	void GpuGridSphSolver::setForceUniforms() const
	{
		GLuint p = mPrograms.force;
		setUnsigned(p, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setGridUniforms(p);
		setFloat(p, "uSmoothingRadius", mParameters.smoothingRadius);
		setFloat(p, "uParticleMass", mParameters.particleMass);
		setFloat(p, "uViscosity", mParameters.viscosity);
		setVec3(p, "uGravity", mParameters.gravity);
	}

	void GpuGridSphSolver::setIntegrateUniforms(float const dt) const
	{
		GLuint p = mPrograms.integrate;
		setUnsigned(p, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setFloat(p, "uFixedDeltaSeconds", dt);
		setFloat(p, "uParticleRadius", mParameters.particleRadius);
		setVec3(p, "uBoundaryCenter", mBoundary.center);
		setVec3(p, "uBoundaryHalfExtent", mBoundary.halfExtent);
		setFloat(p, "uRestitution", mBoundary.restitution);
		setFloat(p, "uFriction", mBoundary.friction);
	}

	void GpuGridSphSolver::dispatchParticles(GLuint const) const
	{
		glDispatchCompute((static_cast<GLuint>(mParticleCount) + kParticleLocalSize - 1u) /
		                  kParticleLocalSize, 1u, 1u);
	}

	void GpuGridSphSolver::dispatchCells(GLuint const, GLuint const local_size) const
	{
		glDispatchCompute((mGridStats.cellCount + local_size - 1u) / local_size, 1u, 1u);
	}

	void GpuGridSphSolver::buildGrid()
	{
		std::uint32_t const zero = 0u;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mCellCountBuffer);
		glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
		std::array<std::uint32_t, 4u> zeros{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mGridErrorBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zeros), zeros.data());
		glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

		glUseProgram(mPrograms.count);
		setUnsigned(mPrograms.count, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setGridUniforms(mPrograms.count);
		dispatchParticles(mPrograms.count);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glUseProgram(mPrograms.scanBlocks);
		setUnsigned(mPrograms.scanBlocks, "uCellCount", mGridStats.cellCount);
		dispatchCells(mPrograms.scanBlocks, kScanLocalSize);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glUseProgram(mPrograms.scanBlockSums);
		setUnsigned(mPrograms.scanBlockSums, "uBlockCount", mGridStats.blockCount);
		glDispatchCompute(1u, 1u, 1u);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glUseProgram(mPrograms.addBlockOffsets);
		setUnsigned(mPrograms.addBlockOffsets, "uCellCount", mGridStats.cellCount);
		dispatchCells(mPrograms.addBlockOffsets, kScanLocalSize);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glUseProgram(mPrograms.prepareScatter);
		setUnsigned(mPrograms.prepareScatter, "uCellCount", mGridStats.cellCount);
		setUnsigned(mPrograms.prepareScatter, "uParticleCount", static_cast<GLuint>(mParticleCount));
		dispatchCells(mPrograms.prepareScatter, kParticleLocalSize);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glUseProgram(mPrograms.scatter);
		setUnsigned(mPrograms.scatter, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setGridUniforms(mPrograms.scatter);
		dispatchParticles(mPrograms.scatter);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}

	GpuGridSphSolver::TimingQuerySlot* GpuGridSphSolver::acquireTimingSlot()
	{
		for (std::size_t offset = 0u; offset < mTimingSlots.size(); ++offset) {
			std::size_t i = (mNextTimingSlot + offset) % mTimingSlots.size();
			if (!mTimingSlots[i].pending) { mNextTimingSlot = (i + 1u) % mTimingSlots.size(); return &mTimingSlots[i]; }
		}
		return nullptr;
	}

	void GpuGridSphSolver::pollTimingQueries()
	{
		for (TimingQuerySlot& slot : mTimingSlots) {
			if (!slot.pending) continue;
			GLint available = GL_FALSE;
			glGetQueryObjectiv(slot.timestamps[4], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available != GL_TRUE) continue;
			std::array<GLuint64, 5u> t{};
			for (std::size_t i = 0u; i < t.size(); ++i)
				glGetQueryObjectui64v(slot.timestamps[i], GL_QUERY_RESULT, &t[i]);
			auto ms = [&](std::size_t a, std::size_t b) { return static_cast<double>(t[b] - t[a]) / 1000000.0; };
			mStageTimings = {};
			mStageTimings.buildGridMilliseconds = ms(0u, 1u);
			mStageTimings.densityPressureMilliseconds = ms(1u, 2u);
			mStageTimings.forceMilliseconds = ms(2u, 3u);
			mStageTimings.integrateMilliseconds = ms(3u, 4u);
			mStageTimings.wholeStepMilliseconds = ms(0u, 4u);
			slot.pending = false;
		}
	}

	void GpuGridSphSolver::rebuildDiagnostics(GpuParticleState const& state)
	{
		std::uint64_t steps = mDiagnostics.completedSteps;
		mDiagnostics = {};
		mDiagnostics.completedSteps = steps;
		if (state.positions.empty()) return;
		mDiagnostics.minimumDensity = std::numeric_limits<float>::max();
		mDiagnostics.totalMass = static_cast<double>(mParameters.particleMass) * state.positions.size();
		double density_sum = 0.0;
		for (std::size_t i = 0u; i < state.positions.size(); ++i) {
			float speed2 = glm::dot(state.velocities[i], state.velocities[i]);
			mDiagnostics.minimumDensity = std::min(mDiagnostics.minimumDensity, state.densities[i]);
			mDiagnostics.maximumDensity = std::max(mDiagnostics.maximumDensity, state.densities[i]);
			density_sum += state.densities[i];
			mDiagnostics.maximumSpeed = std::max(mDiagnostics.maximumSpeed, std::sqrt(std::max(0.0f, speed2)));
			mDiagnostics.kineticEnergy += 0.5 * mParameters.particleMass * speed2;
			mDiagnostics.centerOfMass += state.positions[i];
			mDiagnostics.allFinite = mDiagnostics.allFinite &&
				std::isfinite(state.positions[i].x) && std::isfinite(state.positions[i].y) && std::isfinite(state.positions[i].z) &&
				std::isfinite(state.velocities[i].x) && std::isfinite(state.velocities[i].y) && std::isfinite(state.velocities[i].z) &&
				std::isfinite(state.accelerations[i].x) && std::isfinite(state.accelerations[i].y) && std::isfinite(state.accelerations[i].z) &&
				std::isfinite(state.densities[i]) && std::isfinite(state.pressures[i]);
			for (std::size_t j = 0u; j < state.positions.size(); ++j) {
				if (i == j) continue;
				glm::vec3 d = state.positions[i] - state.positions[j];
				float r2 = glm::dot(d, d);
				if (r2 < mParameters.smoothingRadius * mParameters.smoothingRadius && r2 > 1.0e-12f)
					++mDiagnostics.interactingDirectedPairs;
			}
		}
		mDiagnostics.meanDensity = static_cast<float>(density_sum / state.positions.size());
		mDiagnostics.centerOfMass /= static_cast<float>(state.positions.size());
		mDiagnostics.allFinite = mDiagnostics.allFinite && std::isfinite(mDiagnostics.minimumDensity) &&
			std::isfinite(mDiagnostics.maximumDensity) && std::isfinite(mDiagnostics.meanDensity) &&
			std::isfinite(mDiagnostics.maximumSpeed) && std::isfinite(mDiagnostics.totalMass) &&
			std::isfinite(mDiagnostics.kineticEnergy);
	}
}

#include "simulation/GpuBruteForceSphSolver.hpp"

#include "core/opengl.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace sph
{
	namespace
	{
		constexpr GLuint kPositionBinding = 0u;
		constexpr GLuint kVelocityBinding = 1u;
		constexpr GLuint kAccelerationBinding = 2u;
		constexpr GLuint kDensityBinding = 3u;
		constexpr GLuint kPressureBinding = 4u;

		void allocateBuffer(GLuint& buffer,
		                    GLsizeiptr const byte_size,
		                    char const* const label)
		{
			glGenBuffers(1, &buffer);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, byte_size, nullptr, GL_DYNAMIC_DRAW);
			utils::opengl::debug::nameObject(GL_BUFFER, buffer, label);
		}

		template <typename T>
		void uploadRange(GLuint const buffer,
		                 std::size_t const first,
		                 std::vector<T> const& values)
		{
			if (values.empty())
				return;
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER,
			                static_cast<GLintptr>(first * sizeof(T)),
			                static_cast<GLsizeiptr>(values.size() * sizeof(T)),
			                values.data());
		}

		template <typename T>
		std::vector<T> readBuffer(GLuint const buffer, std::size_t const count)
		{
			std::vector<T> values(count);
			if (count == 0u)
				return values;
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
			                   0,
			                   static_cast<GLsizeiptr>(count * sizeof(T)),
			                   values.data());
			return values;
		}

		void setUnsigned(GLuint const program, char const* const name, GLuint const value)
		{
			glUniform1ui(glGetUniformLocation(program, name), value);
		}

		void setFloat(GLuint const program, char const* const name, float const value)
		{
			glUniform1f(glGetUniformLocation(program, name), value);
		}

		void setVec3(GLuint const program, char const* const name, glm::vec3 const& value)
		{
			glUniform3f(glGetUniformLocation(program, name), value.x, value.y, value.z);
		}
	}

	GpuBruteForceSphSolver::GpuBruteForceSphSolver(
		std::size_t const maximum_particle_count,
		SphParameters const& parameters) :
		mMaximumParticleCount(maximum_particle_count),
		mParameters(parameters)
	{
		if (mMaximumParticleCount == 0u ||
		    mMaximumParticleCount > static_cast<std::size_t>(std::numeric_limits<GLuint>::max()))
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

	GpuBruteForceSphSolver::~GpuBruteForceSphSolver()
	{
		shutdown();
	}

	bool GpuBruteForceSphSolver::isSupported() noexcept
	{
		return GLAD_GL_VERSION_4_3;
	}

	void GpuBruteForceSphSolver::initialize(GLuint const density_program,
	                                        GLuint const force_program,
	                                        GLuint const integrate_program)
	{
		if (!isSupported())
			throw std::runtime_error("M5 requires OpenGL 4.3 compute shaders and SSBOs.");
		shutdown();
		setPrograms(density_program, force_program, integrate_program);

		GLint maximum_invocations = 0;
		glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maximum_invocations);
		if (maximum_invocations < static_cast<GLint>(kLocalSize))
			throw std::runtime_error("GPU compute work-group limit is below 128 invocations.");

		GLint64 maximum_ssbo_size = 0;
		glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maximum_ssbo_size);
		GLsizeiptr const largest_buffer = static_cast<GLsizeiptr>(
			mMaximumParticleCount * sizeof(glm::vec4));
		if (maximum_ssbo_size < largest_buffer)
			throw std::runtime_error("GPU SSBO size limit is below the requested particle capacity.");

		createBuffers();
		for (TimingQuerySlot& slot : mTimingSlots)
			glGenQueries(static_cast<GLsizei>(slot.timestamps.size()), slot.timestamps.data());
		mInitialized = true;
		reset();
	}

	void GpuBruteForceSphSolver::setPrograms(GLuint const density_program,
	                                         GLuint const force_program,
	                                         GLuint const integrate_program)
	{
		if (density_program == 0u || force_program == 0u || integrate_program == 0u)
			throw std::invalid_argument("All M5 compute programs must be valid.");
		mDensityProgram = density_program;
		mForceProgram = force_program;
		mIntegrateProgram = integrate_program;
	}

	void GpuBruteForceSphSolver::shutdown() noexcept
	{
		if (mInitialized) {
			for (TimingQuerySlot& slot : mTimingSlots) {
				glDeleteQueries(static_cast<GLsizei>(slot.timestamps.size()),
				                slot.timestamps.data());
				slot = {};
			}
			GLuint buffers[] = { mPositionBuffer, mVelocityBuffer, mAccelerationBuffer,
			                     mDensityBuffer, mPressureBuffer };
			glDeleteBuffers(5, buffers);
		}
		mPositionBuffer = 0u;
		mVelocityBuffer = 0u;
		mAccelerationBuffer = 0u;
		mDensityBuffer = 0u;
		mPressureBuffer = 0u;
		mParticleCount = 0u;
		mInitialized = false;
	}

	void GpuBruteForceSphSolver::reset()
	{
		mParticleCount = 0u;
		mPositionSnapshot.clear();
		mDiagnostics = {};
		mStageTimings = {};
		mDiagnosticsFresh = true;
		mReadbackCount = 0u;
	}

	std::size_t GpuBruteForceSphSolver::spawnParticles(
		std::vector<ParticleSpawn> const& particles)
	{
		requireInitialized();
		std::size_t const accepted = std::min(
			mMaximumParticleCount - mParticleCount, particles.size());
		mUploadPositions.resize(accepted);
		mUploadVelocities.resize(accepted);
		mUploadAccelerations.assign(accepted, glm::vec4(0.0f));
		mUploadDensities.assign(accepted, mParameters.restDensity);
		mUploadPressures.assign(accepted, 0.0f);
		for (std::size_t index = 0u; index < accepted; ++index) {
			mUploadPositions[index] = glm::vec4(particles[index].position, 1.0f);
			mUploadVelocities[index] = glm::vec4(particles[index].velocity, 0.0f);
			mPositionSnapshot.push_back(particles[index].position);
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

	void GpuBruteForceSphSolver::setBoundary(BoxBoundary const& boundary)
	{
		mBoundary = boundary;
		float const minimum_extent = 2.0f * mParameters.particleRadius;
		for (int axis = 0; axis < 3; ++axis)
			mBoundary.halfExtent[axis] = std::max(minimum_extent, mBoundary.halfExtent[axis]);
		mBoundary.restitution = std::max(0.0f, std::min(1.0f, mBoundary.restitution));
		mBoundary.friction = std::max(0.0f, std::min(1.0f, mBoundary.friction));
	}

	void GpuBruteForceSphSolver::step(float const fixed_delta_seconds)
	{
		stepInternal(fixed_delta_seconds, nullptr);
	}

	void GpuBruteForceSphSolver::stepProfiled(
		float const fixed_delta_seconds,
		std::array<GLuint, 4u> const& timestamp_queries)
	{
		for (GLuint const query : timestamp_queries) {
			if (query == 0u)
				throw std::invalid_argument("Profile timestamp queries must be valid.");
		}
		stepInternal(fixed_delta_seconds, &timestamp_queries);
	}

	void GpuBruteForceSphSolver::stepInternal(
		float const fixed_delta_seconds,
		std::array<GLuint, 4u> const* const timestamp_queries)
	{
		requireInitialized();
		if (!(fixed_delta_seconds > 0.0f) || !std::isfinite(fixed_delta_seconds))
			throw std::invalid_argument("SPH fixed delta must be finite and positive.");
		pollTimingQueries();
		if (mParticleCount == 0u) {
			++mDiagnostics.completedSteps;
			return;
		}

		bindParticleBuffers();
		TimingQuerySlot* const timing =
			timestamp_queries == nullptr ? acquireTimingSlot() : nullptr;
		auto const record_timestamp = [&](std::size_t const index) {
			GLuint const query = timestamp_queries != nullptr
				                     ? (*timestamp_queries)[index]
				                     : (timing != nullptr ? timing->timestamps[index] : 0u);
			if (query != 0u)
				glQueryCounter(query, GL_TIMESTAMP);
		};
		record_timestamp(0u);

		utils::opengl::debug::beginDebugGroup("M5 GPU density-pressure");
		glUseProgram(mDensityProgram);
		setDensityUniforms();
		dispatch(mDensityProgram);
		// Density/pressure SSBO writes are consumed by the force dispatch.
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		record_timestamp(1u);

		utils::opengl::debug::beginDebugGroup("M5 GPU force");
		glUseProgram(mForceProgram);
		setForceUniforms();
		dispatch(mForceProgram);
		// Acceleration SSBO writes are consumed by the integrate dispatch.
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		record_timestamp(2u);

		utils::opengl::debug::beginDebugGroup("M5 GPU integrate");
		glUseProgram(mIntegrateProgram);
		setIntegrateUniforms(fixed_delta_seconds);
		dispatch(mIntegrateProgram);
		// Integrate writes feed the next compute step and the vertex fetch in this frame.
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
		                GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
		utils::opengl::debug::endDebugGroup();
		record_timestamp(3u);
		if (timing != nullptr)
			timing->pending = true;

		glUseProgram(0u);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		++mDiagnostics.completedSteps;
		mDiagnosticsFresh = false;
	}

	std::vector<glm::vec3> const& GpuBruteForceSphSolver::positions() const noexcept
	{
		return mPositionSnapshot;
	}

	std::size_t GpuBruteForceSphSolver::particleCount() const noexcept
	{
		return mParticleCount;
	}

	std::size_t GpuBruteForceSphSolver::capacity() const noexcept
	{
		return mMaximumParticleCount;
	}

	char const* GpuBruteForceSphSolver::backendName() const noexcept
	{
		return "GPU WCSPH Brute Force (OpenGL Compute)";
	}

	float GpuBruteForceSphSolver::particleRadius() const noexcept
	{
		return mParameters.particleRadius;
	}

	SphParameters const& GpuBruteForceSphSolver::parameters() const noexcept
	{
		return mParameters;
	}

	void GpuBruteForceSphSolver::setParameters(SphParameters const& parameters)
	{
		validateParameters(parameters);
		mParameters = parameters;
		setBoundary(mBoundary);
		mDiagnosticsFresh = false;
	}

	SphDiagnostics const& GpuBruteForceSphSolver::diagnostics() const noexcept
	{
		return mDiagnostics;
	}

	SphStageTimings const& GpuBruteForceSphSolver::stageTimings() const noexcept
	{
		return mStageTimings;
	}

	ParticleStorageStats GpuBruteForceSphSolver::storageStats() const noexcept
	{
		constexpr std::size_t bytes_per_particle =
			3u * sizeof(glm::vec4) + 2u * sizeof(float);
		return { mParticleCount,
		         mMaximumParticleCount,
		         bytes_per_particle,
		         mParticleCount * bytes_per_particle,
		         mMaximumParticleCount * bytes_per_particle,
		         false };
	}

	bool GpuBruteForceSphSolver::diagnosticsFresh() const noexcept
	{
		return mDiagnosticsFresh;
	}

	std::uint64_t GpuBruteForceSphSolver::readbackCount() const noexcept
	{
		return mReadbackCount;
	}

	GLuint GpuBruteForceSphSolver::positionBuffer() const noexcept
	{
		return mPositionBuffer;
	}

	GpuParticleState GpuBruteForceSphSolver::readbackState()
	{
		requireInitialized();
		// Shader writes must become visible to the following buffer readback operation.
		glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
		auto const positions = readBuffer<glm::vec4>(mPositionBuffer, mParticleCount);
		auto const velocities = readBuffer<glm::vec4>(mVelocityBuffer, mParticleCount);
		auto const accelerations = readBuffer<glm::vec4>(mAccelerationBuffer, mParticleCount);

		GpuParticleState state;
		state.positions.resize(mParticleCount);
		state.velocities.resize(mParticleCount);
		state.accelerations.resize(mParticleCount);
		for (std::size_t index = 0u; index < mParticleCount; ++index) {
			state.positions[index] = glm::vec3(positions[index]);
			state.velocities[index] = glm::vec3(velocities[index]);
			state.accelerations[index] = glm::vec3(accelerations[index]);
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

	void GpuBruteForceSphSolver::refreshDiagnostics()
	{
		(void)readbackState();
	}

	void GpuBruteForceSphSolver::synchronizeTimings()
	{
		requireInitialized();
		glFinish();
		pollTimingQueries();
	}

	void GpuBruteForceSphSolver::validateParameters(SphParameters const& parameters)
	{
		auto const positive_finite = [](float const value) {
			return value > 0.0f && std::isfinite(value);
		};
		if (!positive_finite(parameters.smoothingRadius) ||
		    !positive_finite(parameters.particleRadius) ||
		    parameters.smoothingRadius <= parameters.particleRadius ||
		    !positive_finite(parameters.restDensity) ||
		    !positive_finite(parameters.particleMass) ||
		    parameters.gasStiffness < 0.0f || !std::isfinite(parameters.gasStiffness) ||
		    parameters.viscosity < 0.0f || !std::isfinite(parameters.viscosity) ||
		    !std::isfinite(parameters.gravity.x) ||
		    !std::isfinite(parameters.gravity.y) ||
		    !std::isfinite(parameters.gravity.z))
			throw std::invalid_argument("Invalid WCSPH parameters.");
	}

	void GpuBruteForceSphSolver::requireInitialized() const
	{
		if (!mInitialized)
			throw std::logic_error("GPU SPH solver must be initialized after creating a GL context.");
	}

	void GpuBruteForceSphSolver::createBuffers()
	{
		allocateBuffer(mPositionBuffer,
		               static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)),
		               "M5 positions SSBO / vertex buffer");
		allocateBuffer(mVelocityBuffer,
		               static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)),
		               "M5 velocities SSBO");
		allocateBuffer(mAccelerationBuffer,
		               static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(glm::vec4)),
		               "M5 accelerations SSBO");
		allocateBuffer(mDensityBuffer,
		               static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(float)),
		               "M5 densities SSBO");
		allocateBuffer(mPressureBuffer,
		               static_cast<GLsizeiptr>(mMaximumParticleCount * sizeof(float)),
		               "M5 pressures SSBO");
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
	}

	void GpuBruteForceSphSolver::bindParticleBuffers() const
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPositionBinding, mPositionBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kVelocityBinding, mVelocityBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kAccelerationBinding, mAccelerationBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kDensityBinding, mDensityBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPressureBinding, mPressureBuffer);
	}

	void GpuBruteForceSphSolver::setDensityUniforms() const
	{
		setUnsigned(mDensityProgram, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setFloat(mDensityProgram, "uSmoothingRadius", mParameters.smoothingRadius);
		setFloat(mDensityProgram, "uParticleMass", mParameters.particleMass);
		setFloat(mDensityProgram, "uRestDensity", mParameters.restDensity);
		setFloat(mDensityProgram, "uGasStiffness", mParameters.gasStiffness);
	}

	void GpuBruteForceSphSolver::setForceUniforms() const
	{
		setUnsigned(mForceProgram, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setFloat(mForceProgram, "uSmoothingRadius", mParameters.smoothingRadius);
		setFloat(mForceProgram, "uParticleMass", mParameters.particleMass);
		setFloat(mForceProgram, "uViscosity", mParameters.viscosity);
		setVec3(mForceProgram, "uGravity", mParameters.gravity);
	}

	void GpuBruteForceSphSolver::setIntegrateUniforms(
		float const fixed_delta_seconds) const
	{
		setUnsigned(mIntegrateProgram, "uParticleCount", static_cast<GLuint>(mParticleCount));
		setFloat(mIntegrateProgram, "uFixedDeltaSeconds", fixed_delta_seconds);
		setFloat(mIntegrateProgram, "uParticleRadius", mParameters.particleRadius);
		setVec3(mIntegrateProgram, "uBoundaryCenter", mBoundary.center);
		setVec3(mIntegrateProgram, "uBoundaryHalfExtent", mBoundary.halfExtent);
		setFloat(mIntegrateProgram, "uRestitution", mBoundary.restitution);
		setFloat(mIntegrateProgram, "uFriction", mBoundary.friction);
	}

	void GpuBruteForceSphSolver::dispatch(GLuint const) const
	{
		GLuint const group_count =
			(static_cast<GLuint>(mParticleCount) + kLocalSize - 1u) / kLocalSize;
		glDispatchCompute(group_count, 1u, 1u);
	}

	GpuBruteForceSphSolver::TimingQuerySlot*
	GpuBruteForceSphSolver::acquireTimingSlot()
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

	void GpuBruteForceSphSolver::pollTimingQueries()
	{
		for (TimingQuerySlot& slot : mTimingSlots) {
			if (!slot.pending)
				continue;
			GLint available = GL_FALSE;
			glGetQueryObjectiv(slot.timestamps[3], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available != GL_TRUE)
				continue;
			std::array<GLuint64, 4u> timestamps{};
			for (std::size_t index = 0u; index < timestamps.size(); ++index)
				glGetQueryObjectui64v(slot.timestamps[index], GL_QUERY_RESULT,
				                      &timestamps[index]);
			auto const milliseconds = [&](std::size_t const begin, std::size_t const end) {
				return static_cast<double>(timestamps[end] - timestamps[begin]) / 1000000.0;
			};
			mStageTimings = {};
			mStageTimings.densityPressureMilliseconds = milliseconds(0u, 1u);
			mStageTimings.forceMilliseconds = milliseconds(1u, 2u);
			mStageTimings.integrateMilliseconds = milliseconds(2u, 3u);
			mStageTimings.wholeStepMilliseconds = milliseconds(0u, 3u);
			slot.pending = false;
		}
	}

	void GpuBruteForceSphSolver::rebuildDiagnostics(GpuParticleState const& state)
	{
		std::uint64_t const completed_steps = mDiagnostics.completedSteps;
		mDiagnostics = {};
		mDiagnostics.completedSteps = completed_steps;
		if (state.positions.empty())
			return;

		mDiagnostics.minimumDensity = std::numeric_limits<float>::max();
		mDiagnostics.totalMass = static_cast<double>(mParameters.particleMass) *
		                         static_cast<double>(state.positions.size());
		double density_sum = 0.0;
		for (std::size_t i = 0u; i < state.positions.size(); ++i) {
			float const squared_speed = glm::dot(state.velocities[i], state.velocities[i]);
			mDiagnostics.minimumDensity =
				std::min(mDiagnostics.minimumDensity, state.densities[i]);
			mDiagnostics.maximumDensity =
				std::max(mDiagnostics.maximumDensity, state.densities[i]);
			density_sum += state.densities[i];
			mDiagnostics.maximumSpeed =
				std::max(mDiagnostics.maximumSpeed, std::sqrt(std::max(0.0f, squared_speed)));
			mDiagnostics.kineticEnergy +=
				0.5 * static_cast<double>(mParameters.particleMass) * squared_speed;
			mDiagnostics.centerOfMass += state.positions[i];
			mDiagnostics.allFinite = mDiagnostics.allFinite &&
				std::isfinite(state.positions[i].x) && std::isfinite(state.positions[i].y) &&
				std::isfinite(state.positions[i].z) && std::isfinite(state.velocities[i].x) &&
				std::isfinite(state.velocities[i].y) && std::isfinite(state.velocities[i].z) &&
				std::isfinite(state.accelerations[i].x) &&
				std::isfinite(state.accelerations[i].y) &&
				std::isfinite(state.accelerations[i].z) &&
				std::isfinite(state.densities[i]) && std::isfinite(state.pressures[i]);

			for (std::size_t j = 0u; j < state.positions.size(); ++j) {
				if (i == j)
					continue;
				glm::vec3 const displacement = state.positions[i] - state.positions[j];
				float const squared_distance = glm::dot(displacement, displacement);
				if (squared_distance < mParameters.smoothingRadius * mParameters.smoothingRadius &&
				    squared_distance > 1.0e-12f)
					++mDiagnostics.interactingDirectedPairs;
			}
		}
		mDiagnostics.meanDensity =
			static_cast<float>(density_sum / static_cast<double>(state.positions.size()));
		mDiagnostics.centerOfMass /= static_cast<float>(state.positions.size());
		mDiagnostics.allFinite = mDiagnostics.allFinite &&
			std::isfinite(mDiagnostics.minimumDensity) &&
			std::isfinite(mDiagnostics.maximumDensity) &&
			std::isfinite(mDiagnostics.meanDensity) &&
			std::isfinite(mDiagnostics.maximumSpeed) &&
			std::isfinite(mDiagnostics.totalMass) &&
			std::isfinite(mDiagnostics.kineticEnergy);
	}
}

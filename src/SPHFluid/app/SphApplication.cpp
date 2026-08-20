#include "app/SphApplication.hpp"

#include "config.hpp"
#include "core/Bonobo.h"
#include "core/helpers.hpp"
#include "core/Log.h"
#include "core/LogView.h"

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace sph
{
	SphApplication::SphApplication(WindowManager& window_manager) :
		mWindowManager(window_manager),
		mInputHandler(),
		mCamera(0.5f * glm::half_pi<float>(),
		        static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
		        0.01f,
		        1000.0f),
		mSolver(65535u << 4),
		mSimulation(mSolver, mScene)
	{
		mScene.emitter.particlesPerSecond = 240.0f;
		mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 2.4f, 6.5f));
		mCamera.mWorld.LookAt(glm::vec3(0.0f, 1.4f, 0.0f));
		mCamera.mMouseSensitivity = glm::vec2(0.003f);
		mCamera.mMovementSpeed = glm::vec3(3.0f);

		WindowManager::WindowDatum window_datum{
			mInputHandler,
			mCamera,
			config::resolution_x,
			config::resolution_y,
			0,
			0,
			0,
			0
		};
		mWindow = mWindowManager.CreateGLFWWindow(
			"SPH Fluid Simulation",
			window_datum,
			config::msaa_rate,
			false,
			true,
			WindowManager::SwapStrategy::enable_vsync);
		if (mWindow == nullptr)
			throw std::runtime_error("Failed to create the SPH Fluid window.");

		bonobo::init();
		mBonoboInitialized = true;
	}

	SphApplication::~SphApplication()
	{
		mScreenSpaceRenderer.shutdown();
		mDebugRenderer.shutdown();
		mRenderer.shutdown();
		if (mBonoboInitialized)
			bonobo::deinit();
	}

	int SphApplication::run()
	{
		if (!GpuGridSphSolver::isSupported()) {
			LogError("GPU SPH requires OpenGL 4.3 compute shaders and shader storage buffers.");
			return EXIT_FAILURE;
		}
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH grid count", "SPHFluid/grid_count.comp", mGridPrograms.count);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH scan blocks", "SPHFluid/grid_scan_blocks.comp", mGridPrograms.scanBlocks);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH scan block sums", "SPHFluid/grid_scan_block_sums.comp", mGridPrograms.scanBlockSums);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH add block offsets", "SPHFluid/grid_add_block_offsets.comp", mGridPrograms.addBlockOffsets);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH prepare scatter", "SPHFluid/grid_prepare_scatter.comp", mGridPrograms.prepareScatter);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH scatter", "SPHFluid/grid_scatter.comp", mGridPrograms.scatter);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH grid density-pressure", "SPHFluid/grid_density_pressure.comp", mGridPrograms.densityPressure);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH grid force", "SPHFluid/grid_force.comp", mGridPrograms.force);
		mProgramManager.CreateAndRegisterComputeProgram(
			"SPH integrate", "SPHFluid/integrate.comp", mGridPrograms.integrate);
		mProgramManager.CreateAndRegisterProgram(
			"SPH particles",
			{ { ShaderType::vertex, "SPHFluid/particle.vert" },
			  { ShaderType::fragment, "SPHFluid/particle.frag" } },
			mParticleProgram);
		mProgramManager.CreateAndRegisterProgram(
			"SPH raw sphere depth",
			{ { ShaderType::vertex, "SPHFluid/raw_depth.vert" },
			  { ShaderType::fragment, "SPHFluid/raw_depth.frag" } },
			mScreenSpacePrograms.rawDepth);
		mProgramManager.CreateAndRegisterProgram(
			"SPH raw sphere thickness",
			{ { ShaderType::vertex, "SPHFluid/raw_depth.vert" },
			  { ShaderType::fragment, "SPHFluid/raw_thickness.frag" } },
			mScreenSpacePrograms.rawThickness);
		mProgramManager.CreateAndRegisterProgram(
			"SPH bilateral scalar filter",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/bilateral_filter.frag" } },
			mScreenSpacePrograms.bilateralFilter);
		mProgramManager.CreateAndRegisterProgram(
			"SPH reconstruct surface normal",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/reconstruct_normal.frag" } },
			mScreenSpacePrograms.reconstructNormal);
		mProgramManager.CreateAndRegisterProgram(
			"SPH screen-space composite",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/screen_present.frag" } },
			mScreenSpacePrograms.present);
		if (mGridPrograms.count == 0u || mGridPrograms.scanBlocks == 0u ||
		    mGridPrograms.scanBlockSums == 0u || mGridPrograms.addBlockOffsets == 0u ||
		    mGridPrograms.prepareScatter == 0u || mGridPrograms.scatter == 0u ||
		    mGridPrograms.densityPressure == 0u || mGridPrograms.force == 0u ||
		    mGridPrograms.integrate == 0u || mParticleProgram == 0u ||
		    mScreenSpacePrograms.rawDepth == 0u ||
		    mScreenSpacePrograms.rawThickness == 0u ||
		    mScreenSpacePrograms.bilateralFilter == 0u ||
		    mScreenSpacePrograms.reconstructNormal == 0u ||
		    mScreenSpacePrograms.present == 0u) {
			LogError("Failed to create a simulation or fluid-render shader program.");
			return EXIT_FAILURE;
		}
		mProgramManager.CreateAndRegisterProgram(
			"SPH scene debug",
			{ { ShaderType::vertex, "SPHFluid/debug.vert" },
			  { ShaderType::fragment, "SPHFluid/debug.frag" } },
			mDebugProgram);
		if (mDebugProgram == 0u) {
			LogError("Failed to create the SPH debug shader program.");
			return EXIT_FAILURE;
		}

		mSolver.initialize(mGridPrograms);
		mRenderer.initializeExternalPositionBuffer(
			mSolver.positionBuffer(), mSolver.capacity(), sizeof(glm::vec4));
		mDebugRenderer.initialize();
		int initial_framebuffer_width = 0;
		int initial_framebuffer_height = 0;
		glfwGetFramebufferSize(mWindow, &initial_framebuffer_width, &initial_framebuffer_height);
		mScreenSpaceRenderer.initialize(mScreenSpacePrograms,
		                                initial_framebuffer_width,
		                                initial_framebuffer_height);
		mScreenSpaceProgramsValid = true;

		glClearDepthf(1.0f);
		glClearColor(0.025f, 0.035f, 0.055f, 1.0f);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_PROGRAM_POINT_SIZE);

		using clock = std::chrono::steady_clock;
		auto previous_time = clock::now();

		while (!glfwWindowShouldClose(mWindow)) {
			auto const current_time = clock::now();
			auto const frame_delta = current_time - previous_time;
			previous_time = current_time;
			double const frame_delta_seconds = std::chrono::duration<double>(frame_delta).count();
			auto const frame_delta_microseconds =
				std::chrono::duration_cast<std::chrono::microseconds>(frame_delta);

			glfwPollEvents();
			ImGuiIO const& io = ImGui::GetIO();
			mInputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
			mInputHandler.Advance();
			mCamera.Update(frame_delta_microseconds, mInputHandler);
			processKeyboardShortcuts();

			int framebuffer_width = 0;
			int framebuffer_height = 0;
			glfwGetFramebufferSize(mWindow, &framebuffer_width, &framebuffer_height);
			if (framebuffer_width > 0 && framebuffer_height > 0)
				mCamera.SetAspect(static_cast<float>(framebuffer_width) /
				                  static_cast<float>(framebuffer_height));

			mWindowManager.NewImGuiFrame();
			drawControls(frame_delta_seconds);
			mScene.emitter.active = mEmitButtonActive;
			mSimulation.advance(frame_delta_seconds);
			mRenderer.setParticleCount(mSolver.particleCount());
			bool const emitter_is_valid = containsSphere(mScene.boundary,
			                                             mScene.emitter.position,
			                                             mScene.emitter.radius);
			mDebugRenderer.update(mScene, emitter_is_valid);

			if (framebuffer_width > 0 && framebuffer_height > 0 &&
			    mScreenSpaceProgramsValid) {
				mScreenSpaceRenderer.beginScene(framebuffer_width, framebuffer_height,
				                                glm::vec3(0.025f, 0.035f, 0.055f));
				if (mFluidDisplayMode == FluidDisplayMode::Points)
					mRenderer.render(mParticleProgram, mCamera.GetWorldToClipMatrix(),
					                 mPointSizePixels);
				if (mDebugGeometryLayer == 0)
					mDebugRenderer.render(mDebugProgram, mCamera.GetWorldToClipMatrix());
				mScreenSpaceRenderer.endScene();

				if (mFluidDisplayMode != FluidDisplayMode::Points) {
					mScreenSpaceRenderer.renderRawDepth(
						mSolver.positionBuffer(), mSolver.particleCount(),
						mSolver.particleRadius(), mCamera.GetWorldToViewMatrix(),
						mCamera.GetViewToClipMatrix(), mCamera.GetClipToViewMatrix());
					bool const normal_mode =
						mFluidDisplayMode == FluidDisplayMode::ViewNormal ||
						mFluidDisplayMode == FluidDisplayMode::WorldNormal;
					bool const material_mode =
						mFluidDisplayMode == FluidDisplayMode::ScreenSpaceDepth ||
						mFluidDisplayMode == FluidDisplayMode::Reflection ||
						mFluidDisplayMode == FluidDisplayMode::Refraction ||
						mFluidDisplayMode == FluidDisplayMode::Transmission;
					bool const needs_thickness =
						material_mode ||
						mFluidDisplayMode == FluidDisplayMode::RawThickness ||
						mFluidDisplayMode == FluidDisplayMode::SmoothThickness;
					if (needs_thickness) {
						mScreenSpaceRenderer.renderRawThickness(
							mSolver.positionBuffer(), mSolver.particleCount(),
							mSolver.particleRadius(), mThicknessScale,
							mCamera.GetWorldToViewMatrix(), mCamera.GetViewToClipMatrix(),
							mCamera.GetClipToViewMatrix());
					}
					bool const needs_smooth_depth =
						material_mode ||
						mFluidDisplayMode == FluidDisplayMode::SmoothDepth ||
						(normal_mode && mNormalFromSmoothDepth);
					if (needs_smooth_depth)
						mScreenSpaceRenderer.smoothDepth(
							mSmoothingParameters, mCamera.GetViewToClipMatrix());
					if (material_mode ||
					    mFluidDisplayMode == FluidDisplayMode::SmoothThickness)
						mScreenSpaceRenderer.smoothThickness(
							mSmoothingParameters, mCamera.GetViewToClipMatrix());
					if (material_mode || normal_mode) {
						NormalDepthSource const source =
							material_mode ||
							mNormalFromSmoothDepth
								? NormalDepthSource::Smoothed : NormalDepthSource::Raw;
						NormalOutputSpace const space =
							mFluidDisplayMode == FluidDisplayMode::WorldNormal
								? NormalOutputSpace::World : NormalOutputSpace::View;
						mScreenSpaceRenderer.reconstructNormals(
							source, space, mCamera.GetClipToViewMatrix(),
							mCamera.GetViewToWorldMatrix());
					}
				}
				mScreenSpaceRenderer.present(mFluidDisplayMode, mRawDepthDisplayScale,
				                             mThicknessDisplayScale, mMaterialParameters,
				                             mCamera.GetClipToViewMatrix(),
				                             mCamera.GetViewToClipMatrix(),
				                             mCamera.GetViewToWorldMatrix());
				if (mDebugGeometryLayer == 1)
					mDebugRenderer.render(mDebugProgram, mCamera.GetWorldToClipMatrix());
				else if (mDebugGeometryLayer == 2) {
					glDisable(GL_DEPTH_TEST);
					mDebugRenderer.render(mDebugProgram, mCamera.GetWorldToClipMatrix());
					glEnable(GL_DEPTH_TEST);
				}
			} else {
				glBindFramebuffer(GL_FRAMEBUFFER, 0u);
				glViewport(0, 0, framebuffer_width, framebuffer_height);
				glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
			}

			if (mShowLogs)
				Log::View::Render();
			mWindowManager.RenderImGuiFrame(mShowGui);
			glfwSwapBuffers(mWindow);
		}

		return EXIT_SUCCESS;
	}

	void SphApplication::processKeyboardShortcuts()
	{
		if (mInputHandler.IsKeyboardCapturedByUI())
			return;

		if (mInputHandler.GetKeycodeState(GLFW_KEY_SPACE) & JUST_RELEASED)
			mSimulation.setPaused(!mSimulation.isPaused());
		if (mInputHandler.GetKeycodeState(GLFW_KEY_N) & JUST_RELEASED)
			mSimulation.stepOnce();
		if (mInputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_RELEASED)
			resetSimulation();
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F5) & JUST_RELEASED) {
			mShaderReloadFailed = !mProgramManager.ReloadAllPrograms();
			bool const simulation_programs_valid =
				mGridPrograms.count != 0u && mGridPrograms.scanBlocks != 0u &&
				mGridPrograms.scanBlockSums != 0u && mGridPrograms.addBlockOffsets != 0u &&
				mGridPrograms.prepareScatter != 0u && mGridPrograms.scatter != 0u &&
				mGridPrograms.densityPressure != 0u && mGridPrograms.force != 0u &&
				mGridPrograms.integrate != 0u;
			mScreenSpaceProgramsValid = mScreenSpacePrograms.rawDepth != 0u &&
			                            mScreenSpacePrograms.rawThickness != 0u &&
			                            mScreenSpacePrograms.bilateralFilter != 0u &&
			                            mScreenSpacePrograms.reconstructNormal != 0u &&
			                            mScreenSpacePrograms.present != 0u;
			if (simulation_programs_valid)
				mSolver.setPrograms(mGridPrograms);
			if (mScreenSpaceProgramsValid)
				mScreenSpaceRenderer.setPrograms(mScreenSpacePrograms);
			if (mShaderReloadFailed || !simulation_programs_valid ||
			    !mScreenSpaceProgramsValid) {
				mSimulation.setPaused(true);
				LogError("Shader reload failed; simulation paused and invalid render passes disabled.");
			}
		}
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
			mShowLogs = !mShowLogs;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
			mShowGui = !mShowGui;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
			mWindowManager.ToggleFullscreenStatusForWindow(mWindow);
	}

	void SphApplication::drawControls(double const frame_delta_seconds)
	{
		mEmitButtonActive = false;
		ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 320.0f),
		                                    ImVec2(1000.0f, 1000.0f));
		bool const opened = ImGui::Begin("SPH Fluid controls", nullptr, ImGuiWindowFlags_None);
		if (opened) {
			ImGui::Text("Backend: %s", mSolver.backendName());
			ImGui::Text("Particles: %zu / %zu", mSolver.particleCount(), mSolver.capacity());
			if (mShaderReloadFailed)
				ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.15f, 1.0f),
				                   "Shader reload failed; fix shaders and press F5.");
			ImGui::Separator();
			if (ImGui::BeginTabBar("SPH controls")) {
				if (ImGui::BeginTabItem("Operate")) {
					drawSimulationControls();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Rendering")) {
					drawRenderingControls();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Performance")) {
					drawPerformanceControls(frame_delta_seconds);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::Separator();
			ImGui::TextUnformatted("Space: pause/resume | N: single step | R: reset");
			ImGui::TextUnformatted("F5: reload shaders | F2: UI | F3: logs | F11: fullscreen");
		}
		ImGui::End();
	}

	void SphApplication::drawSimulationControls()
	{
		bool paused = mSimulation.isPaused();
		if (ImGui::Checkbox("Paused", &paused))
			mSimulation.setPaused(paused);
		ImGui::SameLine();
		if (ImGui::Button("Single step"))
			mSimulation.stepOnce();
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
			resetSimulation();

		ImGui::Text("Steps: %llu | Simulated: %.3f s",
		            static_cast<unsigned long long>(mSimulation.completedSteps()),
		            mSimulation.simulatedSeconds());

		if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Position", &mScene.emitter.position.x, 0.01f, -20.0f, 20.0f);
			ImGui::DragFloat("Radius", &mScene.emitter.radius, 0.005f,
			                 mSolver.particleRadius(), 2.0f, "%.3f m");
			ImGui::DragFloat3("Initial velocity", &mScene.emitter.initialVelocity.x,
			                  0.02f, -20.0f, 20.0f, "%.2f m/s");
			ImGui::DragFloat("Particles / second", &mScene.emitter.particlesPerSecond,
			                 10.0f, 0.0f, 5000.0f, "%.0f");
			int seed = static_cast<int>(mScene.emitter.seed);
			if (ImGui::InputInt("Seed", &seed))
				mScene.emitter.seed = static_cast<std::uint32_t>(std::max(0, seed));

			bool const emitter_is_valid = containsSphere(
				mScene.boundary, mScene.emitter.position, mScene.emitter.radius);
			ImGui::Button("Hold to emit", ImVec2(-1.0f, 34.0f));
			mEmitButtonActive = ImGui::IsItemActive();
			if (!emitter_is_valid)
				ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f),
				                   "Emitter must remain inside the collision box.");
			else if (mSimulation.isPaused())
				ImGui::TextUnformatted("Resume the simulation to emit particles.");
			ImGui::Text("Emitted: %llu | Capacity drops: %llu",
			            static_cast<unsigned long long>(mSimulation.totalEmittedParticles()),
			            static_cast<unsigned long long>(mSimulation.droppedEmissionParticles()));
		}

		if (ImGui::CollapsingHeader("Collision box", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Center", &mScene.boundary.center.x, 0.01f, -20.0f, 20.0f);
			ImGui::DragFloat3("Half extent", &mScene.boundary.halfExtent.x,
			                  0.01f, 0.1f, 20.0f, "%.2f m");
			for (int axis = 0; axis < 3; ++axis)
				mScene.boundary.halfExtent[axis] = std::max(
					2.0f * mSolver.particleRadius(), mScene.boundary.halfExtent[axis]);
			ImGui::SliderFloat("Restitution", &mScene.boundary.restitution, 0.0f, 1.0f);
			ImGui::SliderFloat("Wall friction", &mScene.boundary.friction, 0.0f, 1.0f);
		}

		if (ImGui::CollapsingHeader("Simulation parameters")) {
			SphParameters parameters = mSolver.parameters();
			bool changed = false;
			changed |= ImGui::DragFloat("Smoothing radius", &parameters.smoothingRadius,
			                            0.002f, 0.02f, 1.0f, "%.3f m");
			changed |= ImGui::DragFloat("Particle radius", &parameters.particleRadius,
			                            0.001f, 0.005f, 0.25f, "%.3f m");
			changed |= ImGui::DragFloat("Rest density", &parameters.restDensity,
			                            5.0f, 1.0f, 5000.0f, "%.1f kg/m^3");
			changed |= ImGui::DragFloat("Particle mass", &parameters.particleMass,
			                            0.005f, 0.001f, 20.0f, "%.3f kg");
			changed |= ImGui::DragFloat("Gas stiffness", &parameters.gasStiffness,
			                            1.0f, 0.0f, 5000.0f, "%.1f");
			changed |= ImGui::DragFloat("Viscosity", &parameters.viscosity,
			                            0.05f, 0.0f, 100.0f, "%.2f");
			changed |= ImGui::DragFloat3("Gravity", &parameters.gravity.x,
			                             0.05f, -50.0f, 50.0f, "%.2f m/s^2");
			parameters.particleRadius = std::max(0.005f, parameters.particleRadius);
			parameters.smoothingRadius = std::max(
				parameters.smoothingRadius, parameters.particleRadius + 0.001f);
			if (changed)
				mSolver.setParameters(parameters);
		}
	}

	void SphApplication::drawRenderingControls()
	{
		char const* render_modes[] = {
			"Points", "Composite", "Raw depth", "Raw thickness", "Smooth depth",
			"Smooth thickness", "View-space normal", "World-space normal",
			"Reflection", "Refraction", "Transmission" };
		int render_mode = static_cast<int>(mFluidDisplayMode);
		if (ImGui::Combo("Fluid view", &render_mode, render_modes, 11))
			mFluidDisplayMode = static_cast<FluidDisplayMode>(render_mode);
		if (mFluidDisplayMode == FluidDisplayMode::Points)
			ImGui::SliderFloat("Point size", &mPointSizePixels, 1.0f, 20.0f, "%.1f px");
		else if (mFluidDisplayMode == FluidDisplayMode::RawDepth ||
		         mFluidDisplayMode == FluidDisplayMode::SmoothDepth)
			ImGui::SliderFloat("Depth display range", &mRawDepthDisplayScale,
			                   0.25f, 100.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
		else if (mFluidDisplayMode == FluidDisplayMode::RawThickness ||
		         mFluidDisplayMode == FluidDisplayMode::SmoothThickness)
			ImGui::SliderFloat("Thickness display range", &mThicknessDisplayScale,
			                   0.01f, 20.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);

		char const* debug_layers[] = {
			"In refracted scene", "Depth-tested after fluid", "Always visible", "Hidden" };
		ImGui::Combo("Emitter / box display", &mDebugGeometryLayer, debug_layers, 4);

		if (ImGui::CollapsingHeader("Surface reconstruction", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Thickness scale", &mThicknessScale, 0.0f, 5.0f, "%.3f");
			ImGui::DragFloat("Filter radius", &mSmoothingParameters.worldRadiusMetres,
			                 0.005f, 0.005f, 2.0f, "%.3f m");
			ImGui::SliderInt("Maximum filter radius", &mSmoothingParameters.maximumRadiusPixels,
			                 1, 32, "%d px");
			ImGui::SliderFloat("Spatial sigma", &mSmoothingParameters.spatialSigmaFactor,
			                   0.1f, 2.0f, "%.2f");
			ImGui::DragFloat("Depth falloff", &mSmoothingParameters.depthFalloffPerMetre,
			                 0.5f, 0.0f, 500.0f, "%.1f / m");
			ImGui::SliderInt("Filter iterations", &mSmoothingParameters.iterations, 1, 5);
			ImGui::Checkbox("Use smoothed depth for normals", &mNormalFromSmoothDepth);
		}

		if (ImGui::CollapsingHeader("Fluid material", ImGuiTreeNodeFlags_DefaultOpen)) {
			constexpr float minimum_index_of_refraction = 1.001f;
			constexpr float maximum_index_of_refraction = 2.5f;
			bool const index_changed = ImGui::SliderFloat(
				"Index of refraction", &mIndexOfRefractionEditorValue,
				minimum_index_of_refraction, maximum_index_of_refraction, "%.3f");
			bool const index_is_valid =
				std::isfinite(mIndexOfRefractionEditorValue) &&
				mIndexOfRefractionEditorValue >= minimum_index_of_refraction &&
				mIndexOfRefractionEditorValue <= maximum_index_of_refraction;
			if (index_changed && index_is_valid)
				mMaterialParameters.indexOfRefraction = mIndexOfRefractionEditorValue;
			if (ImGui::IsItemDeactivatedAfterEdit() && !index_is_valid)
				mIndexOfRefractionEditorValue = mMaterialParameters.indexOfRefraction;
			ImGui::ColorEdit3("Absorption (/m)", &mMaterialParameters.absorptionPerMetre.x,
			                  ImGuiColorEditFlags_Float);
			ImGui::ColorEdit3("Scattering colour", &mMaterialParameters.scatteringColour.x,
			                  ImGuiColorEditFlags_Float);
			ImGui::SliderFloat("Refraction scale", &mMaterialParameters.refractionScale,
			                   0.0f, 0.2f, "%.4f");
			ImGui::SliderFloat("Roughness", &mMaterialParameters.roughness,
			                   0.0f, 1.0f, "%.3f");
			ImGui::Checkbox("Reflection", &mMaterialParameters.reflectionEnabled);
			ImGui::SameLine();
			ImGui::Checkbox("Refraction", &mMaterialParameters.refractionEnabled);
			ImGui::SameLine();
			ImGui::Checkbox("Absorption", &mMaterialParameters.absorptionEnabled);
		}
	}

	void SphApplication::drawPerformanceControls(double const frame_delta_seconds)
	{
		double const frame_milliseconds = 1000.0 * frame_delta_seconds;
		double const frames_per_second = frame_delta_seconds > 0.0
			? 1.0 / frame_delta_seconds : 0.0;
		ImGui::Text("CPU frame: %.3f ms (%.1f FPS)", frame_milliseconds, frames_per_second);
		ImGui::Text("Fixed step: %.6f s | Max substeps: %u | Dropped frames: %llu",
		            mSimulation.fixedDeltaSeconds(), mSimulation.maxSubsteps(),
		            static_cast<unsigned long long>(mSimulation.droppedCatchUpFrames()));

		if (ImGui::CollapsingHeader("GPU simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
			SphStageTimings const& timings = mSolver.stageTimings();
			ImGui::Text("Grid / Density / Force / Integrate: %.4f / %.4f / %.4f / %.4f ms",
			            timings.buildGridMilliseconds, timings.densityPressureMilliseconds,
			            timings.forceMilliseconds, timings.integrateMilliseconds);
			ImGui::Text("Phase sum: %.4f ms | Whole step: %.4f ms",
			            timings.totalMilliseconds(), timings.wholeStepMilliseconds);
		}

		if (ImGui::CollapsingHeader("GPU rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
			ScreenSpaceRenderTimings const& timings = mScreenSpaceRenderer.timings();
			ImGui::Text("Scene / Depth / Thickness: %.4f / %.4f / %.4f ms",
			            timings.sceneMilliseconds, timings.rawDepthMilliseconds,
			            timings.rawThicknessMilliseconds);
			ImGui::Text("Smooth depth / thickness: %.4f / %.4f ms",
			            timings.smoothDepthMilliseconds, timings.smoothThicknessMilliseconds);
			ImGui::Text("Normal / Composite / Whole: %.4f / %.4f / %.4f ms",
			            timings.reconstructNormalMilliseconds, timings.presentMilliseconds,
			            timings.wholeRenderMilliseconds);
			RenderPathStats const points =
				mScreenSpaceRenderer.renderPathStats(FluidDisplayMode::Points);
			RenderPathStats const surface =
				mScreenSpaceRenderer.renderPathStats(FluidDisplayMode::ScreenSpaceDepth);
			ImGui::Text("Points whole median/p95 (%zu): %.4f / %.4f ms",
			            points.sampleCount, points.wholeMedianMilliseconds,
			            points.wholeP95Milliseconds);
			ImGui::Text("Surface whole median/p95 (%zu): %.4f / %.4f ms",
			            surface.sampleCount, surface.wholeMedianMilliseconds,
			            surface.wholeP95Milliseconds);
		}

		if (ImGui::CollapsingHeader("Memory")) {
			ParticleStorageStats const storage = mSolver.storageStats();
			ImGui::Text("Particle arrays: %zu B/particle | active %.2f MiB | reserved %.2f MiB",
			            storage.particleArrayBytesPerParticle,
			            static_cast<double>(storage.activeParticleArrayBytes) / (1024.0 * 1024.0),
			            static_cast<double>(storage.reservedParticleArrayBytes) / (1024.0 * 1024.0));
			ImGui::Text("Grid reserved: %.2f MiB",
			            static_cast<double>(mSolver.gridReservedBytes()) / (1024.0 * 1024.0));
			ImGui::Text("Render targets: %d x %d | %.2f MiB | FBO %s",
			            mScreenSpaceRenderer.width(), mScreenSpaceRenderer.height(),
			            static_cast<double>(mScreenSpaceRenderer.reservedTextureBytes()) /
			                (1024.0 * 1024.0),
			            mScreenSpaceRenderer.framebuffersComplete() ? "complete" : "incomplete");
		}

		if (ImGui::CollapsingHeader("Diagnostics snapshots")) {
			if (ImGui::Button("Refresh simulation snapshot"))
				mSolver.refreshDiagnostics();
			ImGui::SameLine();
			ImGui::Text("Readbacks: %llu",
			            static_cast<unsigned long long>(mSolver.readbackCount()));
			SphDiagnostics const& diagnostics = mSolver.diagnostics();
			ImGui::Text("Density min/mean/max: %.2f / %.2f / %.2f kg/m^3",
			            diagnostics.minimumDensity, diagnostics.meanDensity,
			            diagnostics.maximumDensity);
			ImGui::Text("Max speed: %.3f m/s | Kinetic energy: %.6f J",
			            diagnostics.maximumSpeed, diagnostics.kineticEnergy);
			ImGui::TextColored(
				diagnostics.allFinite ? ImVec4(0.3f, 0.95f, 0.45f, 1.0f)
				                      : ImVec4(1.0f, 0.2f, 0.15f, 1.0f),
				diagnostics.allFinite ? "Finite-value check: PASS" : "Finite-value check: FAILED");

			GpuGridStats const& grid = mSolver.gridStats();
			ImGui::Text("Grid: %u cells | nonempty %u | max occupancy %u",
			            grid.cellCount, grid.nonEmptyCellCount, grid.maximumCellOccupancy);
			ImGui::Text("Grid errors coord/scatter/index/prefix: %u / %u / %u / %u",
			            grid.errors[0], grid.errors[1], grid.errors[2], grid.errors[3]);

			if (ImGui::Button("Refresh depth snapshot"))
				mScreenSpaceRenderer.refreshRawDepthStats();
			ImGui::SameLine();
			if (ImGui::Button("Refresh raw thickness"))
				mScreenSpaceRenderer.refreshThicknessStats(false);
			ImGui::SameLine();
			if (ImGui::Button("Refresh smooth thickness"))
				mScreenSpaceRenderer.refreshThicknessStats(true);
			RawDepthStats const& depth = mScreenSpaceRenderer.rawDepthStats();
			ThicknessStats const& thickness = mScreenSpaceRenderer.thicknessStats();
			ImGui::Text("Depth: %.4f .. %.4f m | covered %zu | invalid %zu%s",
			            depth.nearestMetres, depth.farthestMetres, depth.coveredPixelCount,
			            depth.invalidPixelCount, depth.fresh ? "" : " (no snapshot)");
			ImGui::Text("%s thickness mean/max: %.4f / %.4f m | covered %zu | invalid %zu%s",
			            thickness.smoothed ? "Smooth" : "Raw", thickness.meanMetres,
			            thickness.maximumMetres, thickness.coveredPixelCount,
			            thickness.invalidPixelCount, thickness.fresh ? "" : " (no snapshot)");
		}
	}

	void SphApplication::resetSimulation()
	{
		mSimulation.reset();
		mRenderer.setParticleCount(0u);
	}
}

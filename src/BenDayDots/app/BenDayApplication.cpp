#include "app/BenDayApplication.hpp"

#include "config.hpp"
#include "core/helpers.hpp"
#include "core/Log.h"
#include "core/LogView.h"

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>

namespace benday
{
	BenDayApplication::BenDayApplication(WindowManager& window_manager) :
		mWindowManager(window_manager),
		mInputHandler(),
		mCamera(0.5f * glm::half_pi<float>(),
		        static_cast<float>(config::resolution_x) /
		            static_cast<float>(config::resolution_y),
		        1.0f,
		        4000.0f)
	{
		mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 100.0f, 180.0f));
		mCamera.mWorld.LookAt(glm::vec3(0.0f, 100.0f, 0.0f));
		mCamera.mMouseSensitivity = glm::vec2(0.003f);
		mCamera.mMovementSpeed = glm::vec3(200.0f);

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
			"Ben-Day Dots",
			window_datum,
			config::msaa_rate,
			false,
			true,
			WindowManager::SwapStrategy::enable_vsync);
		if (mWindow == nullptr)
			throw std::runtime_error("Failed to create the Ben-Day Dots window.");

		bonobo::init();
		mBonoboInitialized = true;
	}

	BenDayApplication::~BenDayApplication()
	{
		mBenDayPass.shutdown();
		mSceneTarget.shutdown();
		mSceneRenderer.shutdown();
		if (mBonoboInitialized)
			bonobo::deinit();
	}

	int BenDayApplication::run()
	{
		mProgramManager.CreateAndRegisterProgram(
			"Ben-Day scene",
			{ { ShaderType::vertex, "BenDayDots/scene.vert" },
			  { ShaderType::fragment, "BenDayDots/scene.frag" } },
			mSceneProgram);
		mProgramManager.CreateAndRegisterProgram(
			"Ben-Day composite",
			{ { ShaderType::vertex, "BenDayDots/fullscreen.vert" },
			  { ShaderType::fragment, "BenDayDots/benday.frag" } },
			mBenDayProgram);
		mProgramsValid = mSceneProgram != 0u && mBenDayProgram != 0u;
		if (!mProgramsValid) {
			mShaderReloadFailed = true;
			LogError("Failed to create the Ben-Day Dots shader programs.");
		}

		if (!mSceneRenderer.initialize())
			return EXIT_FAILURE;
		mBenDayPass.initialize();

		glClearDepth(1.0);
		glClearColor(0.025f, 0.03f, 0.045f, 1.0f);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		using clock = std::chrono::steady_clock;
		auto previous_time = clock::now();

		while (!glfwWindowShouldClose(mWindow)) {
			auto const current_time = clock::now();
			auto const frame_delta =
				std::chrono::duration_cast<std::chrono::microseconds>(current_time - previous_time);
			previous_time = current_time;

			glfwPollEvents();
			ImGuiIO const& io = ImGui::GetIO();
			mInputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
			mInputHandler.Advance();
			mCamera.Update(frame_delta, mInputHandler);
			processKeyboardShortcuts();

			int framebuffer_width = 0;
			int framebuffer_height = 0;
			glfwGetFramebufferSize(mWindow, &framebuffer_width, &framebuffer_height);
			if (framebuffer_width > 0 && framebuffer_height > 0) {
				mCamera.SetAspect(static_cast<float>(framebuffer_width) /
				                  static_cast<float>(framebuffer_height));
			}

			mWindowManager.NewImGuiFrame();
			if (mShowGui)
				drawControls();

			bool const drawable = framebuffer_width > 0 && framebuffer_height > 0;
			if (drawable && mProgramsValid &&
			    mSceneTarget.resize(framebuffer_width, framebuffer_height)) {
				bool const write_normals =
					mBenDayParameters.enabled && mBenDayParameters.outlinesEnabled;
				mSceneTarget.bindForWriting(write_normals);
				glViewport(0, 0, framebuffer_width, framebuffer_height);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				if (write_normals) {
					float const normal_clear[] = { 0.5f, 0.5f, 1.0f, 1.0f };
					glClearBufferfv(GL_COLOR, 1, normal_clear);
				}
				glEnable(GL_DEPTH_TEST);
				glEnable(GL_CULL_FACE);
				mSceneRenderer.render(mSceneProgram, mCamera.GetWorldToClipMatrix());

				mBenDayPass.render(mBenDayProgram, mSceneTarget.colorTexture(),
				                   mSceneTarget.depthTexture(), mSceneTarget.normalTexture(),
				                   framebuffer_width, framebuffer_height,
				                   mCamera.mNear, mCamera.mFar,
				                   mBenDayParameters);
			} else {
				glBindFramebuffer(GL_FRAMEBUFFER, 0u);
				glViewport(0, 0, framebuffer_width, framebuffer_height);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}

			if (mShowGui && mShowLogs)
				Log::View::Render();
			mWindowManager.RenderImGuiFrame(mShowGui);
			glfwSwapBuffers(mWindow);
		}

		return EXIT_SUCCESS;
	}

	void BenDayApplication::processKeyboardShortcuts()
	{
		if (mInputHandler.IsKeyboardCapturedByUI())
			return;

		if (mInputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
			mShowGui = !mShowGui;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
			mShowLogs = !mShowLogs;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F5) & JUST_RELEASED) {
			mShaderReloadFailed = !mProgramManager.ReloadAllPrograms();
			mProgramsValid = mSceneProgram != 0u && mBenDayProgram != 0u;
			mShaderReloadFailed = mShaderReloadFailed || !mProgramsValid;
			if (!mShaderReloadFailed) {
				mSceneRenderer.invalidateShaderLocations();
				mBenDayPass.invalidateShaderLocations();
			}
		}
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
			mWindowManager.ToggleFullscreenStatusForWindow(mWindow);
	}

	void BenDayApplication::drawControls()
	{
		bool const opened = ImGui::Begin("Ben-Day Dots controls", nullptr,
		                                 ImGuiWindowFlags_AlwaysAutoResize);
		if (opened) {
			ImGuiIO const& io = ImGui::GetIO();
			float const fps = io.Framerate;
			float const frame_time_ms = fps > 0.0f ? 1000.0f / fps : 0.0f;
			ImGui::Text("Performance: %.1f FPS | %.2f ms", fps, frame_time_ms);
			if (mShaderReloadFailed) {
				ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.15f, 1.0f),
				                   "Shader reload failed; fix the shader and press F5.");
			}
			ImGui::Separator();
			ImGui::Checkbox("Enable Ben-Day effect", &mBenDayParameters.enabled);

			char const* color_modes[] = {
				"Monochrome ink on white",
				"RGB additive on black",
				"CMY subtractive on white"
			};
			int color_mode = static_cast<int>(mBenDayParameters.colorMode);
			if (ImGui::Combo("Color mode", &color_mode, color_modes,
			                 IM_ARRAYSIZE(color_modes))) {
				mBenDayParameters.colorMode = static_cast<BenDayColorMode>(color_mode);
			}

			ImGui::SliderFloat("Grid size [px]", &mBenDayParameters.cellSizePixels,
			                   4.0f, 64.0f, "%.1f");
			ImGui::SliderFloat("Exposure [stops]", &mBenDayParameters.exposureStops,
			                   -3.0f, 3.0f, "%.2f");
			ImGui::SliderFloat("Contrast", &mBenDayParameters.contrast,
			                   0.25f, 2.5f, "%.2f");
			ImGui::SliderFloat("Gamma", &mBenDayParameters.gamma,
			                   0.35f, 3.0f, "%.2f");
			ImGui::SliderFloat("Effect intensity", &mBenDayParameters.intensity,
			                   0.0f, 2.0f, "%.2f");

			ImGui::Checkbox("Posterization", &mBenDayParameters.posterizationEnabled);
			if (mBenDayParameters.posterizationEnabled) {
				ImGui::SliderInt("Tone levels", &mBenDayParameters.posterizationLevels,
				                 2, 12);
			}

			ImGui::Checkbox("Depth + normal Sobel outlines",
			                &mBenDayParameters.outlinesEnabled);
			if (mBenDayParameters.outlinesEnabled) {
				ImGui::SliderFloat("Outline strength", &mBenDayParameters.outlineStrength,
				                   0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Depth edge threshold",
				                   &mBenDayParameters.depthOutlineThreshold,
				                   0.005f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Normal edge threshold",
				                   &mBenDayParameters.normalOutlineThreshold,
				                   0.02f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Normal edge contribution",
				                   &mBenDayParameters.normalOutlineWeight,
				                   0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Outline thickness [px]",
				                   &mBenDayParameters.outlineThicknessPixels,
				                   0.5f, 3.0f, "%.1f");
			}

			ImGui::Checkbox("Paper and ink texture",
			                &mBenDayParameters.surfaceTextureEnabled);
			if (mBenDayParameters.surfaceTextureEnabled) {
				ImGui::SliderFloat("Paper grain", &mBenDayParameters.paperGrainStrength,
				                   0.0f, 0.3f, "%.2f");
				ImGui::SliderFloat("Paper grain scale [px]",
				                   &mBenDayParameters.paperGrainScalePixels,
				                   1.0f, 24.0f, "%.1f");
				ImGui::SliderFloat("Ink variation",
				                   &mBenDayParameters.inkVariationStrength,
				                   0.0f, 0.4f, "%.2f");
			}

			if (mBenDayParameters.colorMode == BenDayColorMode::Monochrome) {
				ImGui::SliderFloat("Ink grid angle [deg]",
				                   &mBenDayParameters.anglesDegrees.x,
				                   -180.0f, 180.0f, "%.1f");
				ImGui::SliderFloat("Ink dot scale", &mBenDayParameters.channelDotScales.x,
				                   0.1f, 1.5f, "%.2f");
				ImGui::SliderFloat2("Ink offset [px]",
				                    &mBenDayParameters.registrationOffsets[0].x,
				                    -4.0f, 4.0f, "%.2f");
			} else {
				char const* scale_label =
					mBenDayParameters.colorMode == BenDayColorMode::RgbBlack
						? "RGB dot scales"
						: "CMY dot scales";
				char const* angle_label =
					mBenDayParameters.colorMode == BenDayColorMode::RgbBlack
						? "RGB grid angles [deg]"
						: "CMY grid angles [deg]";
				ImGui::SliderFloat3(angle_label, &mBenDayParameters.anglesDegrees.x,
				                    -180.0f, 180.0f, "%.1f");
				ImGui::SliderFloat3(scale_label, &mBenDayParameters.channelDotScales.x,
				                    0.1f, 1.5f, "%.2f");

				char const* offset_labels_rgb[] = {
					"R offset [px]", "G offset [px]", "B offset [px]"
				};
				char const* offset_labels_cmy[] = {
					"C offset [px]", "M offset [px]", "Y offset [px]"
				};
				char const** offset_labels =
					mBenDayParameters.colorMode == BenDayColorMode::RgbBlack
						? offset_labels_rgb
						: offset_labels_cmy;
				for (std::size_t channel = 0u;
				     channel < mBenDayParameters.registrationOffsets.size(); ++channel) {
					ImGui::SliderFloat2(offset_labels[channel],
					                    &mBenDayParameters.registrationOffsets[channel].x,
					                    -4.0f, 4.0f, "%.2f");
				}
			}
			if (ImGui::Button("Reset effect parameters"))
				mBenDayParameters = BenDayParameters{};
		}
		ImGui::End();
	}
}

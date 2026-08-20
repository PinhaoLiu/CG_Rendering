#pragma once

#include "app/SimulationController.hpp"
#include "rendering/DebugRenderer.hpp"
#include "rendering/ParticleRenderer.hpp"
#include "rendering/ScreenSpaceFluidRenderer.hpp"
#include "simulation/GpuGridSphSolver.hpp"
#include "simulation/SimulationScene.hpp"

#include "core/FPSCamera.h"
#include "core/InputHandler.h"
#include "core/ShaderProgramManager.hpp"
#include "core/WindowManager.hpp"

namespace sph
{
	class SphApplication
	{
	public:
		explicit SphApplication(WindowManager& window_manager);
		~SphApplication();

		SphApplication(SphApplication const&) = delete;
		SphApplication& operator=(SphApplication const&) = delete;

		int run();

	private:
		void processKeyboardShortcuts();
		void drawControls(double frame_delta_seconds);
		void drawSimulationControls();
		void drawRenderingControls();
		void drawPerformanceControls(double frame_delta_seconds);
		void resetSimulation();

		WindowManager& mWindowManager;
		InputHandler mInputHandler;
		FPSCameraf mCamera;
		GLFWwindow* mWindow{ nullptr };

		SimulationScene mScene;
		GpuGridSphSolver mSolver;
		SimulationController mSimulation;
		ParticleRenderer mRenderer;
		DebugRenderer mDebugRenderer;
		ScreenSpaceFluidRenderer mScreenSpaceRenderer;
		ShaderProgramManager mProgramManager;
		GLuint mParticleProgram{ 0u };
		GLuint mDebugProgram{ 0u };
		GpuGridPrograms mGridPrograms;
		ScreenSpaceFluidPrograms mScreenSpacePrograms;

		bool mBonoboInitialized{ false };
		bool mShowGui{ true };
		bool mShowLogs{ false };
		int mDebugGeometryLayer{ 0 };
		bool mEmitButtonActive{ false };
		bool mShaderReloadFailed{ false };
		bool mScreenSpaceProgramsValid{ false };
		bool mNormalFromSmoothDepth{ true };
		float mPointSizePixels{ 7.0f };
		float mRawDepthDisplayScale{ 12.0f };
		float mThicknessScale{ 1.0f };
		float mThicknessDisplayScale{ 1.5f };
		BilateralSmoothingParameters mSmoothingParameters;
		FluidMaterialParameters mMaterialParameters;
		// Keep transient text-entry states (for example "1" while typing "1.33")
		// away from the renderer until they form a valid material parameter.
		float mIndexOfRefractionEditorValue{ 1.33f };
		// Showcase the completed screen-space pipeline; Points remains the reference mode.
		FluidDisplayMode mFluidDisplayMode{ FluidDisplayMode::ScreenSpaceDepth };
	};
}

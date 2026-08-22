#pragma once

#include "rendering/BenDayPass.hpp"
#include "rendering/RenderTarget.hpp"
#include "rendering/SceneRenderer.hpp"

#include "core/FPSCamera.h"
#include "core/InputHandler.h"
#include "core/ShaderProgramManager.hpp"
#include "core/WindowManager.hpp"

namespace benday
{
	class BenDayApplication
	{
	public:
		explicit BenDayApplication(WindowManager& window_manager);
		~BenDayApplication();

		BenDayApplication(BenDayApplication const&) = delete;
		BenDayApplication& operator=(BenDayApplication const&) = delete;

		int run();

	private:
		void processKeyboardShortcuts();
		void drawControls();

		WindowManager& mWindowManager;
		InputHandler mInputHandler;
		FPSCameraf mCamera;
		GLFWwindow* mWindow{ nullptr };
		SceneRenderer mSceneRenderer;
		RenderTarget mSceneTarget;
		BenDayPass mBenDayPass;
		GLuint mSceneProgram{ 0u };
		GLuint mBenDayProgram{ 0u };
		ShaderProgramManager mProgramManager;
		BenDayParameters mBenDayParameters;

		bool mBonoboInitialized{ false };
		bool mShowGui{ true };
		bool mShowLogs{ false };
		bool mProgramsValid{ false };
		bool mShaderReloadFailed{ false };
	};
}

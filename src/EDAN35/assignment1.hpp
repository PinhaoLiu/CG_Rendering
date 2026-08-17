#pragma once

#include "core/FPSCamera.h"
#include "core/InputHandler.h"
#include "core/WindowManager.hpp"

#include <string>

namespace edan35
{
	//! GPU ray tracer migrated from Lab1-RayTracing.
	class Assignment1 {
	public:
		Assignment1(WindowManager& window_manager, std::string model_path);
		~Assignment1();

		void run();

	private:
		FPSCameraf mCamera;
		InputHandler mInputHandler;
		WindowManager& mWindowManager;
		GLFWwindow* mWindow;
		std::string mModelPath;
	};
}

add_executable (BenDay_Dots)

target_sources (
	BenDay_Dots
	PRIVATE
		[[main.cpp]]
		[[app/BenDayApplication.cpp]]
		[[app/BenDayApplication.hpp]]
		[[rendering/BenDayPass.cpp]]
		[[rendering/BenDayPass.hpp]]
		[[rendering/RenderTarget.cpp]]
		[[rendering/RenderTarget.hpp]]
		[[rendering/SceneRenderer.cpp]]
		[[rendering/SceneRenderer.hpp]]
)

target_include_directories (BenDay_Dots PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries (BenDay_Dots PRIVATE assignment_setup)

copy_dlls (BenDay_Dots "${CMAKE_CURRENT_BINARY_DIR}")
install (TARGETS BenDay_Dots DESTINATION bin)

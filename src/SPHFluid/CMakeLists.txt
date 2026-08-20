find_package (Threads REQUIRED)

add_executable (SPH_Fluid)

target_sources (
	SPH_Fluid
	PRIVATE
		[[main.cpp]]
		[[app/SphApplication.cpp]]
		[[app/SphApplication.hpp]]
		[[app/SimulationController.cpp]]
		[[app/SimulationController.hpp]]
		[[rendering/DebugRenderer.cpp]]
		[[rendering/DebugRenderer.hpp]]
		[[rendering/ParticleRenderer.cpp]]
		[[rendering/ParticleRenderer.hpp]]
		[[rendering/ScreenSpaceFluidRenderer.cpp]]
		[[rendering/ScreenSpaceFluidRenderer.hpp]]
		[[simulation/CpuDodSphSolver.cpp]]
		[[simulation/CpuDodSphSolver.hpp]]
		[[simulation/CpuGridSphSolver.cpp]]
		[[simulation/CpuGridSphSolver.hpp]]
		[[simulation/CpuParallelSphSolver.cpp]]
		[[simulation/CpuParallelSphSolver.hpp]]
		[[simulation/CpuSphSolver.cpp]]
		[[simulation/CpuSphSolver.hpp]]
		[[simulation/InteractiveParticleSolver.cpp]]
		[[simulation/InteractiveParticleSolver.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/FixedThreadPool.cpp]]
		[[simulation/FixedThreadPool.hpp]]
		[[simulation/GpuBruteForceSphSolver.cpp]]
		[[simulation/GpuBruteForceSphSolver.hpp]]
		[[simulation/GpuGridSphSolver.cpp]]
		[[simulation/GpuGridSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ParticleEmitter.cpp]]
		[[simulation/ParticleEmitter.hpp]]
		[[simulation/ParticleSoA.cpp]]
		[[simulation/ParticleSoA.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphKernels.cpp]]
		[[simulation/SphKernels.hpp]]
		[[simulation/SphTypes.hpp]]
		[[simulation/StaticParticleSolver.cpp]]
		[[simulation/StaticParticleSolver.hpp]]
		[[simulation/UniformGrid.cpp]]
		[[simulation/UniformGrid.hpp]]
)

target_include_directories (SPH_Fluid PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid PRIVATE assignment_setup Threads::Threads)

copy_dlls (SPH_Fluid "${CMAKE_CURRENT_BINARY_DIR}")
install (TARGETS SPH_Fluid DESTINATION bin)

option (SPH_FLUID_BUILD_TESTS "Build local SPH Fluid validation targets." OFF)

if (SPH_FLUID_BUILD_TESTS)
add_executable (SPH_Fluid_Tests)
target_sources (
	SPH_Fluid_Tests
	PRIVATE
		[[tests/InteractiveParticleTests.cpp]]
		[[app/SimulationController.cpp]]
		[[app/SimulationController.hpp]]
		[[simulation/CpuDodSphSolver.cpp]]
		[[simulation/CpuDodSphSolver.hpp]]
		[[simulation/CpuGridSphSolver.cpp]]
		[[simulation/CpuGridSphSolver.hpp]]
		[[simulation/CpuParallelSphSolver.cpp]]
		[[simulation/CpuParallelSphSolver.hpp]]
		[[simulation/CpuSphSolver.cpp]]
		[[simulation/CpuSphSolver.hpp]]
		[[simulation/InteractiveParticleSolver.cpp]]
		[[simulation/InteractiveParticleSolver.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/FixedThreadPool.cpp]]
		[[simulation/FixedThreadPool.hpp]]
		[[simulation/ParticleEmitter.cpp]]
		[[simulation/ParticleEmitter.hpp]]
		[[simulation/ParticleSoA.cpp]]
		[[simulation/ParticleSoA.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphKernels.cpp]]
		[[simulation/SphKernels.hpp]]
		[[simulation/SphTypes.hpp]]
		[[simulation/StaticParticleSolver.cpp]]
		[[simulation/StaticParticleSolver.hpp]]
		[[simulation/UniformGrid.cpp]]
		[[simulation/UniformGrid.hpp]]
)
target_include_directories (SPH_Fluid_Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Tests PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Tests PRIVATE CG_Labs_options glm Threads::Threads)
endif ()

add_executable (SPH_Fluid_Benchmarks)
target_sources (
	SPH_Fluid_Benchmarks
	PRIVATE
		[[profiling/SphBenchmark.cpp]]
		[[simulation/CpuParallelSphSolver.cpp]]
		[[simulation/CpuParallelSphSolver.hpp]]
		[[simulation/FixedThreadPool.cpp]]
		[[simulation/FixedThreadPool.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/ParticleEmitter.hpp]]
		[[simulation/ParticleSoA.cpp]]
		[[simulation/ParticleSoA.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphKernels.cpp]]
		[[simulation/SphKernels.hpp]]
		[[simulation/SphTypes.hpp]]
		[[simulation/UniformGrid.cpp]]
		[[simulation/UniformGrid.hpp]]
)
target_include_directories (SPH_Fluid_Benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Benchmarks PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Benchmarks PRIVATE CG_Labs_options glm Threads::Threads)

if (SPH_FLUID_BUILD_TESTS)
add_executable (SPH_Fluid_Gpu_Tests)
target_sources (
	SPH_Fluid_Gpu_Tests
	PRIVATE
		[[tests/GpuBruteForceTests.cpp]]
		[[simulation/CpuSphSolver.cpp]]
		[[simulation/CpuSphSolver.hpp]]
		[[simulation/GpuBruteForceSphSolver.cpp]]
		[[simulation/GpuBruteForceSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphKernels.cpp]]
		[[simulation/SphKernels.hpp]]
		[[simulation/SphTypes.hpp]]
)
target_include_directories (SPH_Fluid_Gpu_Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Gpu_Tests PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Gpu_Tests PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_Gpu_Tests "${CMAKE_CURRENT_BINARY_DIR}")

add_executable (SPH_Fluid_Gpu_Grid_Tests)
target_sources (
	SPH_Fluid_Gpu_Grid_Tests
	PRIVATE
		[[tests/GpuGridTests.cpp]]
		[[simulation/GpuBruteForceSphSolver.cpp]]
		[[simulation/GpuBruteForceSphSolver.hpp]]
		[[simulation/GpuGridSphSolver.cpp]]
		[[simulation/GpuGridSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphTypes.hpp]]
)
target_include_directories (SPH_Fluid_Gpu_Grid_Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Gpu_Grid_Tests PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Gpu_Grid_Tests PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_Gpu_Grid_Tests "${CMAKE_CURRENT_BINARY_DIR}")

add_executable (SPH_Fluid_ScreenSpace_Tests)
target_sources (
	SPH_Fluid_ScreenSpace_Tests
	PRIVATE
		[[tests/ScreenSpaceFluidTests.cpp]]
		[[rendering/ScreenSpaceFluidRenderer.cpp]]
		[[rendering/ScreenSpaceFluidRenderer.hpp]]
)
target_include_directories (SPH_Fluid_ScreenSpace_Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_ScreenSpace_Tests PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_ScreenSpace_Tests PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_ScreenSpace_Tests "${CMAKE_CURRENT_BINARY_DIR}")
endif ()

add_executable (SPH_Fluid_Gpu_Benchmarks)
target_sources (
	SPH_Fluid_Gpu_Benchmarks
	PRIVATE
		[[profiling/GpuSphBenchmark.cpp]]
		[[simulation/GpuBruteForceSphSolver.cpp]]
		[[simulation/GpuBruteForceSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphTypes.hpp]]
)
target_include_directories (SPH_Fluid_Gpu_Benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Gpu_Benchmarks PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Gpu_Benchmarks PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_Gpu_Benchmarks "${CMAKE_CURRENT_BINARY_DIR}")

add_executable (SPH_Fluid_Gpu_Grid_Benchmarks)
target_sources (
	SPH_Fluid_Gpu_Grid_Benchmarks
	PRIVATE
		[[profiling/GpuGridBenchmark.cpp]]
		[[simulation/GpuBruteForceSphSolver.cpp]]
		[[simulation/GpuBruteForceSphSolver.hpp]]
		[[simulation/GpuGridSphSolver.cpp]]
		[[simulation/GpuGridSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphTypes.hpp]]
)
target_include_directories (SPH_Fluid_Gpu_Grid_Benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features (SPH_Fluid_Gpu_Grid_Benchmarks PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Gpu_Grid_Benchmarks PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_Gpu_Grid_Benchmarks "${CMAKE_CURRENT_BINARY_DIR}")

add_executable (SPH_Fluid_Render_Benchmarks)
target_sources (
	SPH_Fluid_Render_Benchmarks
	PRIVATE
		[[profiling/ScreenSpaceRenderBenchmark.cpp]]
		[[rendering/ParticleRenderer.cpp]]
		[[rendering/ParticleRenderer.hpp]]
		[[rendering/ScreenSpaceFluidRenderer.cpp]]
		[[rendering/ScreenSpaceFluidRenderer.hpp]]
		[[simulation/GpuGridSphSolver.cpp]]
		[[simulation/GpuGridSphSolver.hpp]]
		[[simulation/GpuSphTypes.hpp]]
		[[simulation/ISphSolver.hpp]]
		[[simulation/SimulationScene.hpp]]
		[[simulation/SphTypes.hpp]]
)
target_include_directories (
	SPH_Fluid_Render_Benchmarks
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_SOURCE_DIR}/dependencies/stb-src"
)
target_compile_features (SPH_Fluid_Render_Benchmarks PRIVATE cxx_std_17)
target_link_libraries (SPH_Fluid_Render_Benchmarks PRIVATE assignment_setup)
copy_dlls (SPH_Fluid_Render_Benchmarks "${CMAKE_CURRENT_BINARY_DIR}")

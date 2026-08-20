#include "simulation/GpuBruteForceSphSolver.hpp"

#include "core/ShaderProgramManager.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	struct Options
	{
		std::size_t particleCount{ 1024u };
		std::size_t warmupSteps{ 100u };
		std::size_t measuredSteps{ 1000u };
		std::size_t runCount{ 3u };
		std::string csvPath{ "m5-gpu-benchmark.csv" };
		std::string commitLabel{ "uncommitted" };
	};

	struct Sample
	{
		std::size_t run{ 0u };
		std::size_t step{ 0u };
		double densityMilliseconds{ 0.0 };
		double forceMilliseconds{ 0.0 };
		double integrateMilliseconds{ 0.0 };
		double wholeMilliseconds{ 0.0 };
	};

	std::size_t parsePositiveSize(char const* const text, char const* const option)
	{
		if (text[0] == '-')
			throw std::invalid_argument(std::string(option) + " must be positive.");
		std::size_t consumed = 0u;
		unsigned long long const value = std::stoull(text, &consumed);
		if (text[consumed] != '\0' || value == 0u ||
		    value > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument(std::string(option) + " must be positive.");
		return static_cast<std::size_t>(value);
	}

	Options parseOptions(int const argc, char const* const* const argv)
	{
		Options options;
		for (int argument = 1; argument < argc; ++argument) {
			std::string const name = argv[argument];
			if (name == "--help") {
				std::cout << "SPH_Fluid_Gpu_Benchmarks --particles N --warmup N "
				             "--steps N --runs N --csv FILE --commit HASH\n";
				std::exit(0);
			}
			if (argument + 1 >= argc)
				throw std::invalid_argument("Missing value for " + name + '.');
			char const* const value = argv[++argument];
			if (name == "--particles")
				options.particleCount = parsePositiveSize(value, "--particles");
			else if (name == "--warmup")
				options.warmupSteps = parsePositiveSize(value, "--warmup");
			else if (name == "--steps")
				options.measuredSteps = parsePositiveSize(value, "--steps");
			else if (name == "--runs")
				options.runCount = parsePositiveSize(value, "--runs");
			else if (name == "--csv")
				options.csvPath = value;
			else if (name == "--commit")
				options.commitLabel = value;
			else
				throw std::invalid_argument("Unknown option: " + name);
		}
		return options;
	}

	std::vector<sph::ParticleSpawn> makeLattice(std::size_t const particle_count,
	                                            float const spacing)
	{
		std::size_t const side = static_cast<std::size_t>(
			std::ceil(std::cbrt(static_cast<double>(particle_count))));
		float const offset = 0.5f * static_cast<float>(side - 1u) * spacing;
		std::vector<sph::ParticleSpawn> particles;
		particles.reserve(particle_count);
		for (std::size_t z = 0u; z < side && particles.size() < particle_count; ++z) {
			for (std::size_t y = 0u; y < side && particles.size() < particle_count; ++y) {
				for (std::size_t x = 0u; x < side && particles.size() < particle_count; ++x) {
					particles.push_back({ glm::vec3(static_cast<float>(x) * spacing - offset,
					                                      static_cast<float>(y) * spacing - offset,
					                                      static_cast<float>(z) * spacing - offset),
					                      glm::vec3(0.0f) });
				}
			}
		}
		return particles;
	}

	double percentile(std::vector<double> values, double const fraction)
	{
		std::sort(values.begin(), values.end());
		std::size_t const rank = static_cast<std::size_t>(
			std::ceil(fraction * static_cast<double>(values.size())));
		return values[std::max<std::size_t>(1u, rank) - 1u];
	}

	double milliseconds(GLuint64 const begin, GLuint64 const end)
	{
		return static_cast<double>(end - begin) / 1000000.0;
	}

	char const* glString(GLenum const name)
	{
		return reinterpret_cast<char const*>(glGetString(name));
	}

	char const* buildConfiguration() noexcept
	{
#ifdef NDEBUG
		return "Release";
#else
		return "Debug";
#endif
	}

	std::string csvString(std::string const& value)
	{
		std::string escaped{ '"' };
		for (char const character : value) {
			if (character == '"')
				escaped.push_back('"');
			escaped.push_back(character);
		}
		escaped.push_back('"');
		return escaped;
	}
}

int main(int const argc, char const* const* const argv)
{
	try {
		Options const options = parseOptions(argc, argv);
		if (glfwInit() != GLFW_TRUE)
			throw std::runtime_error("GLFW initialization failed.");
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		GLFWwindow* const window = glfwCreateWindow(
			64, 64, "SPH M5 GPU benchmark", nullptr, nullptr);
		if (window == nullptr)
			throw std::runtime_error("Hidden OpenGL 4.6 context creation failed.");
		glfwMakeContextCurrent(window);
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
			throw std::runtime_error("GLAD initialization failed.");
		if (!sph::GpuBruteForceSphSolver::isSupported())
			throw std::runtime_error("OpenGL 4.3 compute/SSBO support is unavailable.");

		std::vector<Sample> samples;
		samples.reserve(options.runCount * options.measuredSteps);
		{
			ShaderProgramManager programs;
			GLuint density_program = 0u;
			GLuint force_program = 0u;
			GLuint integrate_program = 0u;
			programs.CreateAndRegisterComputeProgram(
				"M5 benchmark density", "SPHFluid/density_pressure.comp", density_program);
			programs.CreateAndRegisterComputeProgram(
				"M5 benchmark force", "SPHFluid/force.comp", force_program);
			programs.CreateAndRegisterComputeProgram(
				"M5 benchmark integrate", "SPHFluid/integrate.comp", integrate_program);
			if (density_program == 0u || force_program == 0u || integrate_program == 0u)
				throw std::runtime_error("M5 compute shader compilation failed.");

			sph::SphParameters parameters;
			parameters.gravity = glm::vec3(0.0f);
			float const spacing = 0.5f * parameters.smoothingRadius;
			auto const particles = makeLattice(options.particleCount, spacing);
			std::size_t const side = static_cast<std::size_t>(
				std::ceil(std::cbrt(static_cast<double>(options.particleCount))));
			float const half_width =
				0.5f * static_cast<float>(side - 1u) * spacing + 1.0f;
			sph::BoxBoundary boundary;
			boundary.center = glm::vec3(0.0f);
			boundary.halfExtent = glm::vec3(half_width);
			boundary.restitution = 0.0f;
			boundary.friction = 0.0f;

			for (std::size_t run = 0u; run < options.runCount; ++run) {
				sph::GpuBruteForceSphSolver solver(options.particleCount, parameters);
				solver.initialize(density_program, force_program, integrate_program);
				solver.setBoundary(boundary);
				if (solver.spawnParticles(particles) != particles.size())
					throw std::runtime_error("GPU benchmark rejected initial particles.");
				for (std::size_t step = 0u; step < options.warmupSteps; ++step)
					solver.step(1.0f / 120.0f);
				solver.synchronizeTimings();

				std::vector<GLuint> queries(options.measuredSteps * 4u);
				glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
				for (std::size_t step = 0u; step < options.measuredSteps; ++step) {
					std::array<GLuint, 4u> const step_queries{
						queries[4u * step], queries[4u * step + 1u],
						queries[4u * step + 2u], queries[4u * step + 3u]
					};
					solver.stepProfiled(1.0f / 120.0f, step_queries);
				}
				// Resolve the whole batch once; the measured loop never waits on a query.
				glFinish();
				for (std::size_t step = 0u; step < options.measuredSteps; ++step) {
					std::array<GLuint64, 4u> timestamps{};
					for (std::size_t point = 0u; point < timestamps.size(); ++point)
						glGetQueryObjectui64v(queries[4u * step + point],
						                      GL_QUERY_RESULT, &timestamps[point]);
					samples.push_back({ run, step,
					                    milliseconds(timestamps[0], timestamps[1]),
					                    milliseconds(timestamps[1], timestamps[2]),
					                    milliseconds(timestamps[2], timestamps[3]),
					                    milliseconds(timestamps[0], timestamps[3]) });
				}
				glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
				solver.refreshDiagnostics();
				if (!solver.diagnostics().allFinite)
					throw std::runtime_error("GPU benchmark produced non-finite state.");
			}
		}

		std::ofstream csv(options.csvPath, std::ios::trunc);
		if (!csv)
			throw std::runtime_error("Could not open CSV output: " + options.csvPath);
		csv << "schema_version,scene,build_type,gpu_vendor,gpu_renderer,driver,commit,"
		       "particles,warmup_steps,measured_steps,run_count,run_id,measured_step,"
		       "fixed_dt_seconds,density_pressure_ms,force_ms,integrate_ms,whole_step_ms\n";
		csv << std::setprecision(9);
		for (Sample const& sample : samples) {
			csv << "1," << csvString("stable-lattice") << ','
			    << csvString(buildConfiguration()) << ','
			    << csvString(glString(GL_VENDOR)) << ','
			    << csvString(glString(GL_RENDERER)) << ','
			    << csvString(glString(GL_VERSION)) << ','
			    << csvString(options.commitLabel) << ',' << options.particleCount << ','
			    << options.warmupSteps << ',' << options.measuredSteps << ','
			    << options.runCount << ',' << sample.run << ',' << sample.step << ','
			    << (1.0 / 120.0) << ',' << sample.densityMilliseconds << ','
			    << sample.forceMilliseconds << ',' << sample.integrateMilliseconds << ','
			    << sample.wholeMilliseconds << '\n';
		}

		auto summarize = [&](char const* const label, auto const selector) {
			std::vector<double> values;
			values.reserve(samples.size());
			for (Sample const& sample : samples)
				values.push_back(selector(sample));
			std::cout << label << " median=" << std::fixed << std::setprecision(4)
			          << percentile(values, 0.50) << " ms p95="
			          << percentile(values, 0.95) << " ms\n";
		};

		std::cout << "M5 GPU benchmark: " << glString(GL_VENDOR) << " | "
		          << glString(GL_RENDERER) << " | " << glString(GL_VERSION) << '\n';
		std::cout << "particles=" << options.particleCount
		          << " warmup=" << options.warmupSteps
		          << " measured=" << options.measuredSteps
		          << " runs=" << options.runCount << '\n';
		summarize("density", [](Sample const& sample) { return sample.densityMilliseconds; });
		summarize("force", [](Sample const& sample) { return sample.forceMilliseconds; });
		summarize("integrate", [](Sample const& sample) { return sample.integrateMilliseconds; });
		summarize("whole", [](Sample const& sample) { return sample.wholeMilliseconds; });
		std::cout << "Raw samples: " << options.csvPath << '\n';

		glfwDestroyWindow(window);
		glfwTerminate();
		return 0;
	} catch (std::exception const& error) {
		std::cerr << "M5 GPU benchmark failed: " << error.what() << '\n';
		glfwTerminate();
		return 1;
	}
}

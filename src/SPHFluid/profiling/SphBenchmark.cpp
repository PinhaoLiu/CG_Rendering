#include "simulation/CpuParallelSphSolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	struct Options
	{
		std::size_t particleCount{ 4096u };
		std::size_t warmupSteps{ 100u };
		std::size_t measuredSteps{ 1000u };
		std::size_t runCount{ 3u };
		std::string csvPath{ "m4-benchmark.csv" };
		std::string hardwareLabel{ "unspecified" };
		std::string commitLabel{ "uncommitted" };
	};

	std::size_t parsePositiveSize(char const* const text,
	                              char const* const option_name)
	{
		if (text[0] == '-')
			throw std::invalid_argument(std::string(option_name) +
			                            " must be a positive integer.");
		std::size_t consumed = 0u;
		unsigned long long const value = std::stoull(text, &consumed);
		if (text[consumed] != '\0' || value == 0u ||
		    value > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument(std::string(option_name) +
			                            " must be a positive integer.");
		return static_cast<std::size_t>(value);
	}

	Options parseOptions(int const argc, char const* const* const argv)
	{
		Options options;
		for (int argument = 1; argument < argc; ++argument) {
			std::string const name = argv[argument];
			if (name == "--help") {
				std::cout << "SPH_Fluid_Benchmarks --particles N --warmup N "
				             "--steps N --runs N --csv FILE --hardware LABEL --commit HASH\n";
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
			else if (name == "--hardware")
				options.hardwareLabel = value;
			else if (name == "--commit")
				options.commitLabel = value;
			else
				throw std::invalid_argument("Unknown option: " + name);
		}
		if (options.csvPath.empty())
			throw std::invalid_argument("--csv path must not be empty.");
		return options;
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

	char const* buildConfiguration() noexcept
	{
#ifdef NDEBUG
		return "Release";
#else
		return "Debug";
#endif
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
					particles.push_back({ glm::vec3(
						static_cast<float>(x) * spacing - offset,
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

	std::vector<double> wholeStepSamples(
		std::vector<sph::SphStageTimings> const& samples)
	{
		std::vector<double> values;
		values.reserve(samples.size());
		for (sph::SphStageTimings const& sample : samples)
			values.push_back(sample.wholeStepMilliseconds);
		return values;
	}
}

int main(int const argc, char const* const* const argv)
{
	try {
		Options const options = parseOptions(argc, argv);
		std::size_t const maximum_threads = std::max<std::size_t>(
			1u, std::thread::hardware_concurrency());
		std::vector<std::size_t> thread_counts{ 1u, 2u, 4u, maximum_threads };
		for (std::size_t& count : thread_counts)
			count = std::min(count, maximum_threads);
		std::sort(thread_counts.begin(), thread_counts.end());
		thread_counts.erase(std::unique(thread_counts.begin(), thread_counts.end()),
		                    thread_counts.end());

		sph::SphParameters parameters;
		parameters.gravity = glm::vec3(0.0f);
		float const spacing = 0.5f * parameters.smoothingRadius;
		auto const particles = makeLattice(options.particleCount, spacing);
		std::size_t const side = static_cast<std::size_t>(
			std::ceil(std::cbrt(static_cast<double>(options.particleCount))));
		float const half_width = 0.5f * static_cast<float>(side - 1u) * spacing + 1.0f;
		sph::BoxBoundary boundary;
		boundary.center = glm::vec3(0.0f);
		boundary.halfExtent = glm::vec3(half_width);
		boundary.restitution = 0.0f;
		boundary.friction = 0.0f;

		std::ofstream csv(options.csvPath, std::ios::trunc);
		if (!csv)
			throw std::runtime_error("Could not open CSV output: " + options.csvPath);
		csv << "schema_version,scene,build_type,hardware,commit,max_threads,threads,"
		       "particles,warmup_steps,measured_steps,run_count,run_id,measured_step,"
		       "fixed_dt_seconds,"
		       "smoothing_radius,particle_radius,rest_density,particle_mass,"
		       "gas_stiffness,viscosity,gravity_x,gravity_y,gravity_z,build_grid_ms,"
		       "density_pressure_ms,force_ms,integrate_ms,diagnostics_ms,whole_step_ms\n";
		csv << std::setprecision(9);

		std::cout << "M4 benchmark: particles=" << options.particleCount
		          << ", warmup=" << options.warmupSteps
		          << ", measured=" << options.measuredSteps
		          << ", runs=" << options.runCount
		          << ", max_threads=" << maximum_threads
		          << ", hardware=" << options.hardwareLabel
		          << ", commit=" << options.commitLabel << '\n';
		std::cout << "Percentiles use nearest-rank samples.\n";

		double single_thread_median = 0.0;
		for (std::size_t const thread_count : thread_counts) {
			std::vector<sph::SphStageTimings> samples;
			samples.reserve(options.runCount * options.measuredSteps);
			for (std::size_t run = 0u; run < options.runCount; ++run) {
				sph::CpuParallelSphSolver solver(
					options.particleCount, parameters, maximum_threads);
				solver.setThreadCount(thread_count);
				solver.setBoundary(boundary);
				if (solver.spawnParticles(particles) != particles.size())
					throw std::runtime_error("Benchmark solver rejected initial particles.");
				for (std::size_t step = 0u; step < options.warmupSteps; ++step)
					solver.step(1.0f / 120.0f);

				std::vector<sph::SphStageTimings> run_samples;
				run_samples.reserve(options.measuredSteps);
				for (std::size_t step = 0u; step < options.measuredSteps; ++step) {
					solver.step(1.0f / 120.0f);
					run_samples.push_back(solver.stageTimings());
				}
				if (!solver.diagnostics().allFinite)
					throw std::runtime_error("Benchmark scene produced a non-finite result.");

				for (std::size_t step = 0u; step < run_samples.size(); ++step) {
					sph::SphStageTimings const& sample = run_samples[step];
					csv << "2," << csvString("stable-lattice") << ','
					    << csvString(buildConfiguration()) << ','
					    << csvString(options.hardwareLabel) << ','
					    << csvString(options.commitLabel) << ','
					    << maximum_threads << ',' << thread_count << ','
					    << options.particleCount << ',' << options.warmupSteps << ','
					    << options.measuredSteps << ',' << options.runCount << ','
					    << run << ',' << step << ',' << (1.0 / 120.0) << ','
					    << parameters.smoothingRadius << ','
					    << parameters.particleRadius << ','
					    << parameters.restDensity << ','
					    << parameters.particleMass << ','
					    << parameters.gasStiffness << ','
					    << parameters.viscosity << ','
					    << parameters.gravity.x << ','
					    << parameters.gravity.y << ','
					    << parameters.gravity.z << ','
					    << sample.buildGridMilliseconds << ','
					    << sample.densityPressureMilliseconds << ','
					    << sample.forceMilliseconds << ','
					    << sample.integrateMilliseconds << ','
					    << sample.diagnosticsMilliseconds << ','
					    << sample.wholeStepMilliseconds << '\n';
				}
				samples.insert(samples.end(), run_samples.begin(), run_samples.end());
			}

			auto const whole_step_samples = wholeStepSamples(samples);
			double const median = percentile(whole_step_samples, 0.50);
			double const p95 = percentile(whole_step_samples, 0.95);
			if (thread_count == 1u)
				single_thread_median = median;
			double const speedup = single_thread_median / median;
			double const efficiency = speedup / static_cast<double>(thread_count);
			std::cout << "threads=" << thread_count
			          << " median=" << std::fixed << std::setprecision(4) << median
			          << " ms p95=" << p95
			          << " ms speedup=" << speedup
			          << " efficiency=" << efficiency << '\n';
		}
		std::cout << "Raw samples: " << options.csvPath << '\n';
		return 0;
	} catch (std::exception const& exception) {
		std::cerr << "Benchmark failed: " << exception.what() << '\n';
		return 1;
	}
}

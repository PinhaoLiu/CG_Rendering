#include "simulation/GpuBruteForceSphSolver.hpp"
#include "simulation/GpuGridSphSolver.hpp"

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
		std::size_t particles{ 4096u }, warmup{ 100u }, steps{ 300u }, runs{ 3u };
		std::string csv{ "m6-gpu-grid-benchmark.csv" }, commit{ "uncommitted" };
	};
	struct Sample
	{
		std::string backend;
		std::size_t run, step;
		double grid, density, force, integrate, whole;
	};

	std::size_t positive(char const* text, char const* option)
	{
		if (text[0] == '-') throw std::invalid_argument(std::string(option) + " must be positive.");
		std::size_t used = 0u;
		auto value = std::stoull(text, &used);
		if (text[used] != '\0' || value == 0u || value > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument(std::string(option) + " must be positive.");
		return static_cast<std::size_t>(value);
	}

	Options parse(int argc, char const* const* argv)
	{
		Options o;
		for (int i = 1; i < argc; ++i) {
			std::string name = argv[i];
			if (name == "--help") {
				std::cout << "SPH_Fluid_Gpu_Grid_Benchmarks --particles N --warmup N --steps N --runs N --csv FILE --commit HASH\n";
				std::exit(0);
			}
			if (++i >= argc) throw std::invalid_argument("Missing value for " + name);
			if (name == "--particles") o.particles = positive(argv[i], "--particles");
			else if (name == "--warmup") o.warmup = positive(argv[i], "--warmup");
			else if (name == "--steps") o.steps = positive(argv[i], "--steps");
			else if (name == "--runs") o.runs = positive(argv[i], "--runs");
			else if (name == "--csv") o.csv = argv[i];
			else if (name == "--commit") o.commit = argv[i];
			else throw std::invalid_argument("Unknown option: " + name);
		}
		return o;
	}

	std::vector<sph::ParticleSpawn> lattice(std::size_t count, float spacing)
	{
		std::size_t side = static_cast<std::size_t>(std::ceil(std::cbrt(static_cast<double>(count))));
		float offset = 0.5f * static_cast<float>(side - 1u) * spacing;
		std::vector<sph::ParticleSpawn> result;
		result.reserve(count);
		for (std::size_t z = 0; z < side && result.size() < count; ++z)
		for (std::size_t y = 0; y < side && result.size() < count; ++y)
		for (std::size_t x = 0; x < side && result.size() < count; ++x)
			result.push_back({ glm::vec3(x * spacing - offset, y * spacing - offset, z * spacing - offset), glm::vec3(0.0f) });
		return result;
	}

	double ms(GLuint64 a, GLuint64 b) { return static_cast<double>(b - a) / 1.0e6; }
	double percentile(std::vector<double> values, double fraction)
	{
		std::sort(values.begin(), values.end());
		std::size_t rank = static_cast<std::size_t>(std::ceil(fraction * values.size()));
		return values[std::max<std::size_t>(1u, rank) - 1u];
	}
	char const* gls(GLenum n) { return reinterpret_cast<char const*>(glGetString(n)); }
	char const* config() {
#ifdef NDEBUG
		return "Release";
#else
		return "Debug";
#endif
	}
	std::string quoted(std::string const& s)
	{
		std::string r = "\"";
		for (char c : s) { if (c == '"') r += '"'; r += c; }
		return r + '"';
	}

	void create(ShaderProgramManager& m, char const* label, char const* path, GLuint& p)
	{
		m.CreateAndRegisterComputeProgram(label, path, p);
	}

	void collectM5(Options const& o, sph::SphParameters const& params,
	               sph::BoxBoundary const& boundary,
	               std::vector<sph::ParticleSpawn> const& particles,
	               GLuint density, GLuint force, GLuint integrate,
	               std::vector<Sample>& samples)
	{
		for (std::size_t run = 0; run < o.runs; ++run) {
			sph::GpuBruteForceSphSolver solver(o.particles, params);
			solver.initialize(density, force, integrate);
			solver.setBoundary(boundary);
			solver.spawnParticles(particles);
			for (std::size_t i = 0; i < o.warmup; ++i) solver.step(1.0f / 120.0f);
			std::vector<GLuint> queries(o.steps * 4u);
			glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
			for (std::size_t step = 0; step < o.steps; ++step) {
				std::array<GLuint, 4> q{ queries[4*step], queries[4*step+1], queries[4*step+2], queries[4*step+3] };
				solver.stepProfiled(1.0f / 120.0f, q);
			}
			glFinish();
			for (std::size_t step = 0; step < o.steps; ++step) {
				std::array<GLuint64, 4> t{};
				for (std::size_t i = 0; i < 4; ++i) glGetQueryObjectui64v(queries[4*step+i], GL_QUERY_RESULT, &t[i]);
				samples.push_back({ "m5_bruteforce", run, step, 0.0, ms(t[0],t[1]), ms(t[1],t[2]), ms(t[2],t[3]), ms(t[0],t[3]) });
			}
			glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
		}
	}

	void collectM6(Options const& o, sph::SphParameters const& params,
	               sph::BoxBoundary const& boundary,
	               std::vector<sph::ParticleSpawn> const& particles,
	               sph::GpuGridPrograms const& programs, std::vector<Sample>& samples)
	{
		for (std::size_t run = 0; run < o.runs; ++run) {
			sph::GpuGridSphSolver solver(o.particles, params);
			solver.initialize(programs);
			solver.setBoundary(boundary);
			solver.spawnParticles(particles);
			for (std::size_t i = 0; i < o.warmup; ++i) solver.step(1.0f / 120.0f);
			std::vector<GLuint> queries(o.steps * 5u);
			glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
			for (std::size_t step = 0; step < o.steps; ++step) {
				std::array<GLuint, 5> q{ queries[5*step], queries[5*step+1], queries[5*step+2], queries[5*step+3], queries[5*step+4] };
				solver.stepProfiled(1.0f / 120.0f, q);
			}
			glFinish();
			for (std::size_t step = 0; step < o.steps; ++step) {
				std::array<GLuint64, 5> t{};
				for (std::size_t i = 0; i < 5; ++i) glGetQueryObjectui64v(queries[5*step+i], GL_QUERY_RESULT, &t[i]);
				samples.push_back({ "m6_grid", run, step, ms(t[0],t[1]), ms(t[1],t[2]), ms(t[2],t[3]), ms(t[3],t[4]), ms(t[0],t[4]) });
			}
			glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
		}
	}
}

int main(int argc, char const* const* argv)
{
	try {
		Options o = parse(argc, argv);
		if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed.");
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		GLFWwindow* window = glfwCreateWindow(64,64,"SPH M6 benchmark",nullptr,nullptr);
		if (!window) throw std::runtime_error("Hidden OpenGL context creation failed.");
		glfwMakeContextCurrent(window);
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("GLAD failed.");

		std::vector<Sample> samples;
		{
			ShaderProgramManager manager;
			GLuint m5d=0,m5f=0,integrate=0;
			create(manager,"M5 benchmark density","SPHFluid/density_pressure.comp",m5d);
			create(manager,"M5 benchmark force","SPHFluid/force.comp",m5f);
			create(manager,"benchmark integrate","SPHFluid/integrate.comp",integrate);
			sph::GpuGridPrograms p;
			create(manager,"M6 count","SPHFluid/grid_count.comp",p.count);
			create(manager,"M6 scan blocks","SPHFluid/grid_scan_blocks.comp",p.scanBlocks);
			create(manager,"M6 scan sums","SPHFluid/grid_scan_block_sums.comp",p.scanBlockSums);
			create(manager,"M6 add offsets","SPHFluid/grid_add_block_offsets.comp",p.addBlockOffsets);
			create(manager,"M6 prepare","SPHFluid/grid_prepare_scatter.comp",p.prepareScatter);
			create(manager,"M6 scatter","SPHFluid/grid_scatter.comp",p.scatter);
			create(manager,"M6 density","SPHFluid/grid_density_pressure.comp",p.densityPressure);
			create(manager,"M6 force","SPHFluid/grid_force.comp",p.force);
			p.integrate = integrate;
			sph::SphParameters params; params.gravity = glm::vec3(0.0f);
			float spacing = 0.5f * params.smoothingRadius;
			auto particles = lattice(o.particles, spacing);
			std::size_t side = static_cast<std::size_t>(std::ceil(std::cbrt(static_cast<double>(o.particles))));
			sph::BoxBoundary boundary; boundary.center = glm::vec3(0.0f);
			boundary.halfExtent = glm::vec3(0.5f * static_cast<float>(side - 1u) * spacing + 1.0f);
			boundary.restitution = boundary.friction = 0.0f;
			collectM5(o,params,boundary,particles,m5d,m5f,integrate,samples);
			collectM6(o,params,boundary,particles,p,samples);
		}

		std::ofstream csv(o.csv, std::ios::trunc);
		if (!csv) throw std::runtime_error("Could not open CSV output.");
		csv << "schema_version,scene,backend,build_type,gpu_vendor,gpu_renderer,driver,commit,particles,warmup_steps,measured_steps,run_count,run_id,measured_step,fixed_dt_seconds,build_grid_ms,density_pressure_ms,force_ms,integrate_ms,whole_step_ms\n";
		csv << std::setprecision(9);
		for (Sample const& s : samples)
			csv << "2," << quoted("stable-lattice") << ',' << quoted(s.backend) << ',' << quoted(config()) << ','
			    << quoted(gls(GL_VENDOR)) << ',' << quoted(gls(GL_RENDERER)) << ',' << quoted(gls(GL_VERSION)) << ','
			    << quoted(o.commit) << ',' << o.particles << ',' << o.warmup << ',' << o.steps << ',' << o.runs << ','
			    << s.run << ',' << s.step << ',' << (1.0/120.0) << ',' << s.grid << ',' << s.density << ','
			    << s.force << ',' << s.integrate << ',' << s.whole << '\n';

		std::cout << "M6 comparison benchmark: " << gls(GL_RENDERER) << " particles=" << o.particles << '\n';
		for (std::string backend : { std::string("m5_bruteforce"), std::string("m6_grid") }) {
			std::vector<double> whole;
			for (Sample const& s : samples) if (s.backend == backend) whole.push_back(s.whole);
			std::cout << backend << " whole median=" << std::fixed << std::setprecision(4)
			          << percentile(whole,.5) << " ms p95=" << percentile(whole,.95) << " ms\n";
		}
		std::cout << "Raw samples: " << o.csv << '\n';
		glfwDestroyWindow(window); glfwTerminate(); return 0;
	} catch (std::exception const& e) {
		std::cerr << "M6 GPU benchmark failed: " << e.what() << '\n'; glfwTerminate(); return 1;
	}
}

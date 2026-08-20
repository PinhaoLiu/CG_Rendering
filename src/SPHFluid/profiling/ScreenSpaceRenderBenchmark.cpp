#include "rendering/ParticleRenderer.hpp"
#include "rendering/ScreenSpaceFluidRenderer.hpp"
#include "simulation/GpuGridSphSolver.hpp"

#include "core/ShaderProgramManager.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	struct Options
	{
		int width{ 1280 };
		int height{ 720 };
		std::size_t particles{ 1024u };
		int iterations{ 2 };
		std::size_t warmup{ 10u };
		std::size_t frames{ 30u };
		std::size_t runs{ 3u };
		std::string backend{ "frozen_renderer" };
		std::string mode{ "composite" };
		std::string csv{ "m7-render-benchmark.csv" };
		std::string commit{ "working-tree" };
		std::string capturePrefix;
	};

	struct Sample
	{
		std::size_t run{ 0u };
		std::size_t frame{ 0u };
		std::size_t coveredPixels{ 0u };
		double simulation{ 0.0 };
		double scene{ 0.0 };
		double depth{ 0.0 };
		double thickness{ 0.0 };
		double smoothDepth{ 0.0 };
		double smoothThickness{ 0.0 };
		double normal{ 0.0 };
		double composite{ 0.0 };
		double wholeRender{ 0.0 };
		double endToEnd{ 0.0 };
	};

	std::size_t positive(char const* text, char const* option)
	{
		if (text[0] == '-') throw std::invalid_argument(std::string(option) + " must be positive.");
		std::size_t used = 0u;
		unsigned long long const value = std::stoull(text, &used);
		if (text[used] != '\0' || value == 0u ||
		    value > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument(std::string(option) + " must be positive.");
		return static_cast<std::size_t>(value);
	}

	Options parse(int argc, char const* const* argv)
	{
		Options result;
		for (int argument = 1; argument < argc; ++argument) {
			std::string const name = argv[argument];
			if (name == "--help") {
				std::cout
					<< "SPH_Fluid_Render_Benchmarks --width N --height N --particles N "
					   "--iterations N --warmup N --frames N --runs N "
					   "--backend frozen_renderer|m6_end_to_end "
					   "--mode points|composite --csv FILE --commit HASH "
					   "[--capture-prefix PATH]\n";
				std::exit(0);
			}
			if (++argument >= argc) throw std::invalid_argument("Missing value for " + name + '.');
			char const* const value = argv[argument];
			if (name == "--width") result.width = static_cast<int>(positive(value, "--width"));
			else if (name == "--height") result.height = static_cast<int>(positive(value, "--height"));
			else if (name == "--particles") result.particles = positive(value, "--particles");
			else if (name == "--iterations") result.iterations = static_cast<int>(positive(value, "--iterations"));
			else if (name == "--warmup") result.warmup = positive(value, "--warmup");
			else if (name == "--frames") result.frames = positive(value, "--frames");
			else if (name == "--runs") result.runs = positive(value, "--runs");
			else if (name == "--backend") result.backend = value;
			else if (name == "--mode") result.mode = value;
			else if (name == "--csv") result.csv = value;
			else if (name == "--commit") result.commit = value;
			else if (name == "--capture-prefix") result.capturePrefix = value;
			else throw std::invalid_argument("Unknown option: " + name);
		}
		if (result.width > 7680 || result.height > 4320)
			throw std::invalid_argument("Benchmark framebuffer is limited to 7680x4320.");
		if (result.iterations < 1 || result.iterations > 5)
			throw std::invalid_argument("--iterations must be in [1,5].");
		if (result.backend != "frozen_renderer" && result.backend != "m6_end_to_end")
			throw std::invalid_argument("--backend must be frozen_renderer or m6_end_to_end.");
		if (result.mode != "points" && result.mode != "composite")
			throw std::invalid_argument("--mode must be points or composite.");
		return result;
	}

	std::vector<sph::ParticleSpawn> lattice(std::size_t const count, float const spacing)
	{
		std::size_t const side = static_cast<std::size_t>(
			std::ceil(std::cbrt(static_cast<double>(count))));
		float const offset = 0.5f * static_cast<float>(side - 1u) * spacing;
		std::vector<sph::ParticleSpawn> result;
		result.reserve(count);
		for (std::size_t z = 0u; z < side && result.size() < count; ++z)
		for (std::size_t y = 0u; y < side && result.size() < count; ++y)
		for (std::size_t x = 0u; x < side && result.size() < count; ++x)
			result.push_back({ glm::vec3(static_cast<float>(x) * spacing - offset,
			                             static_cast<float>(y) * spacing - offset,
			                             static_cast<float>(z) * spacing - offset - 5.0f),
			                   glm::vec3(0.0f) });
		return result;
	}

	void createPrograms(ShaderProgramManager& manager,
	                    sph::ScreenSpaceFluidPrograms& render,
	                    GLuint& benchmark_scene,
	                    GLuint& particle,
	                    sph::GpuGridPrograms* grid)
	{
		manager.CreateAndRegisterProgram("M7 benchmark fixed scene",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/benchmark_scene.frag" } }, benchmark_scene);
		manager.CreateAndRegisterProgram("M7 benchmark particles",
			{ { ShaderType::vertex, "SPHFluid/particle.vert" },
			  { ShaderType::fragment, "SPHFluid/particle.frag" } }, particle);
		manager.CreateAndRegisterProgram("M7 benchmark raw depth",
			{ { ShaderType::vertex, "SPHFluid/raw_depth.vert" },
			  { ShaderType::fragment, "SPHFluid/raw_depth.frag" } }, render.rawDepth);
		manager.CreateAndRegisterProgram("M7 benchmark thickness",
			{ { ShaderType::vertex, "SPHFluid/raw_depth.vert" },
			  { ShaderType::fragment, "SPHFluid/raw_thickness.frag" } }, render.rawThickness);
		manager.CreateAndRegisterProgram("M7 benchmark filter",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/bilateral_filter.frag" } }, render.bilateralFilter);
		manager.CreateAndRegisterProgram("M7 benchmark normal",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/reconstruct_normal.frag" } }, render.reconstructNormal);
		manager.CreateAndRegisterProgram("M7 benchmark composite",
			{ { ShaderType::vertex, "SPHFluid/screen_present.vert" },
			  { ShaderType::fragment, "SPHFluid/screen_present.frag" } }, render.present);
		if (benchmark_scene == 0u || particle == 0u || render.rawDepth == 0u || render.rawThickness == 0u ||
		    render.bilateralFilter == 0u || render.reconstructNormal == 0u ||
		    render.present == 0u)
			throw std::runtime_error("M7 benchmark shader compilation failed.");
		if (grid == nullptr) return;
		auto compute = [&](char const* label, char const* path, GLuint& program) {
			manager.CreateAndRegisterComputeProgram(label, path, program);
		};
		compute("M7 benchmark grid count", "SPHFluid/grid_count.comp", grid->count);
		compute("M7 benchmark scan blocks", "SPHFluid/grid_scan_blocks.comp", grid->scanBlocks);
		compute("M7 benchmark scan sums", "SPHFluid/grid_scan_block_sums.comp", grid->scanBlockSums);
		compute("M7 benchmark add offsets", "SPHFluid/grid_add_block_offsets.comp", grid->addBlockOffsets);
		compute("M7 benchmark prepare scatter", "SPHFluid/grid_prepare_scatter.comp", grid->prepareScatter);
		compute("M7 benchmark scatter", "SPHFluid/grid_scatter.comp", grid->scatter);
		compute("M7 benchmark density", "SPHFluid/grid_density_pressure.comp", grid->densityPressure);
		compute("M7 benchmark force", "SPHFluid/grid_force.comp", grid->force);
		compute("M7 benchmark integrate", "SPHFluid/integrate.comp", grid->integrate);
		if (grid->count == 0u || grid->scanBlocks == 0u || grid->scanBlockSums == 0u ||
		    grid->addBlockOffsets == 0u || grid->prepareScatter == 0u ||
		    grid->scatter == 0u || grid->densityPressure == 0u ||
		    grid->force == 0u || grid->integrate == 0u)
			throw std::runtime_error("M6 end-to-end benchmark shader compilation failed.");
	}

	double milliseconds(GLuint64 const begin, GLuint64 const end)
	{
		return static_cast<double>(end - begin) / 1000000.0;
	}

	double percentile(std::vector<double> values, double const fraction)
	{
		std::sort(values.begin(), values.end());
		std::size_t const rank = static_cast<std::size_t>(
			std::ceil(fraction * static_cast<double>(values.size())));
		return values[std::max<std::size_t>(1u, rank) - 1u];
	}

	char const* glString(GLenum const name)
	{
		return reinterpret_cast<char const*>(glGetString(name));
	}

	char const* buildType()
	{
#ifdef NDEBUG
		return "Release";
#else
		return "Debug";
#endif
	}

	std::string quote(std::string const& value)
	{
		std::string result{ '"' };
		for (char const character : value) {
			if (character == '"') result.push_back('"');
			result.push_back(character);
		}
		result.push_back('"');
		return result;
	}

	void capturePng(std::string const& path, int const width, int const height)
	{
		std::vector<unsigned char> pixels(static_cast<std::size_t>(width) *
		                                  static_cast<std::size_t>(height) * 4u);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		std::size_t const row_bytes = static_cast<std::size_t>(width) * 4u;
		std::vector<unsigned char> row(row_bytes);
		for (int y = 0; y < height / 2; ++y) {
			auto const lower = pixels.begin() + static_cast<std::ptrdiff_t>(y) *
			                           static_cast<std::ptrdiff_t>(row_bytes);
			auto const upper = pixels.begin() + static_cast<std::ptrdiff_t>(height - 1 - y) *
			                           static_cast<std::ptrdiff_t>(row_bytes);
			std::copy_n(lower, row_bytes, row.begin());
			std::copy_n(upper, row_bytes, lower);
			std::copy_n(row.begin(), row_bytes, upper);
		}
		if (stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) == 0)
			throw std::runtime_error("Could not write capture: " + path);
	}
}

int main(int argc, char const* const* argv)
{
	try {
		Options const options = parse(argc, argv);
		if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed.");
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		GLFWwindow* const window = glfwCreateWindow(
			options.width, options.height, "SPH M7 render benchmark", nullptr, nullptr);
		if (window == nullptr) throw std::runtime_error("Hidden OpenGL context creation failed.");
		glfwMakeContextCurrent(window);
		glfwSwapInterval(0);
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
			throw std::runtime_error("GLAD initialization failed.");

		std::vector<Sample> samples;
		ShaderProgramManager manager;
		sph::ScreenSpaceFluidPrograms render_programs;
		sph::GpuGridPrograms grid_programs;
		GLuint benchmark_scene_program = 0u;
		GLuint particle_program = 0u;
		createPrograms(manager, render_programs, benchmark_scene_program, particle_program,
		               options.backend == "m6_end_to_end" ? &grid_programs : nullptr);
		GLuint benchmark_scene_vertex_array = 0u;
		glGenVertexArrays(1, &benchmark_scene_vertex_array);
		sph::SphParameters parameters;
		parameters.gravity = glm::vec3(0.0f);
		float const particle_spacing = 0.35f * parameters.smoothingRadius;
		auto const particles = lattice(options.particles, particle_spacing);
		glm::mat4 const view(1.0f);
		glm::mat4 const projection = glm::perspective(
			glm::radians(60.0f), static_cast<float>(options.width) /
			static_cast<float>(options.height), 0.01f, 100.0f);
		glm::mat4 const inverse_projection = glm::inverse(projection);
		sph::BilateralSmoothingParameters smoothing;
		smoothing.iterations = options.iterations;
		sph::FluidMaterialParameters material;

		for (std::size_t run = 0u; run < options.runs; ++run) {
			GLuint frozen_buffer = 0u;
			std::unique_ptr<sph::GpuGridSphSolver> solver;
			GLuint position_buffer = 0u;
			float particle_radius = parameters.particleRadius;
			if (options.backend == "frozen_renderer") {
				std::vector<glm::vec4> positions;
				positions.reserve(particles.size());
				for (sph::ParticleSpawn const& particle : particles)
					positions.emplace_back(particle.position, 1.0f);
				glGenBuffers(1, &frozen_buffer);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, frozen_buffer);
				glBufferData(GL_SHADER_STORAGE_BUFFER,
				             static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec4)),
				             positions.data(), GL_STATIC_DRAW);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
				position_buffer = frozen_buffer;
			} else {
				solver = std::make_unique<sph::GpuGridSphSolver>(options.particles, parameters);
				solver->initialize(grid_programs);
				sph::BoxBoundary boundary;
				boundary.center = glm::vec3(0.0f, 0.0f, -5.0f);
				boundary.halfExtent = glm::vec3(4.0f);
				boundary.restitution = 0.0f;
				boundary.friction = 0.0f;
				solver->setBoundary(boundary);
				if (solver->spawnParticles(particles) != particles.size())
					throw std::runtime_error("M6 benchmark rejected initial particles.");
				position_buffer = solver->positionBuffer();
				particle_radius = solver->particleRadius();
			}

			sph::ScreenSpaceFluidRenderer renderer;
			renderer.initialize(render_programs, options.width, options.height);
			sph::ParticleRenderer point_renderer;
			point_renderer.initializeExternalPositionBuffer(
				position_buffer, options.particles, sizeof(glm::vec4));
			point_renderer.setParticleCount(options.particles);

			auto render_frame = [&](std::array<GLuint, 9u> const* queries) {
				auto stamp = [&](std::size_t const index) {
					if (queries != nullptr) glQueryCounter((*queries)[index], GL_TIMESTAMP);
				};
				stamp(0u);
				if (solver) solver->step(1.0f / 120.0f);
				stamp(1u);
				renderer.beginScene(options.width, options.height,
				                    glm::vec3(0.025f, 0.035f, 0.055f));
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE);
				glUseProgram(benchmark_scene_program);
				glBindVertexArray(benchmark_scene_vertex_array);
				glDrawArrays(GL_TRIANGLES, 0, 3);
				glBindVertexArray(0u);
				glUseProgram(0u);
				glDepthMask(GL_TRUE);
				glEnable(GL_DEPTH_TEST);
				if (options.mode == "points")
					point_renderer.render(particle_program, projection * view, 5.0f);
				renderer.endScene();
				stamp(2u);
				if (options.mode == "composite") {
					renderer.renderRawDepth(position_buffer, options.particles, particle_radius,
					                        view, projection, inverse_projection);
					stamp(3u);
					renderer.renderRawThickness(position_buffer, options.particles,
					                            particle_radius, 1.0f, view, projection,
					                            inverse_projection);
					stamp(4u);
					renderer.smoothDepth(smoothing, projection);
					stamp(5u);
					renderer.smoothThickness(smoothing, projection);
					stamp(6u);
					renderer.reconstructNormals(sph::NormalDepthSource::Smoothed,
					                            sph::NormalOutputSpace::View,
					                            inverse_projection, glm::mat4(1.0f));
					stamp(7u);
					renderer.present(sph::FluidDisplayMode::ScreenSpaceDepth,
					                 12.0f, 1.5f, material, inverse_projection,
					                 projection, glm::mat4(1.0f));
				} else {
					for (std::size_t index = 3u; index <= 7u; ++index) stamp(index);
					renderer.present(sph::FluidDisplayMode::Points, 12.0f);
				}
				stamp(8u);
			};

			for (std::size_t frame = 0u; frame < options.warmup; ++frame) render_frame(nullptr);
			if (run == 0u && options.mode == "composite" &&
			    !options.capturePrefix.empty()) {
				struct CaptureMode { sph::FluidDisplayMode mode; char const* suffix; };
				for (CaptureMode const capture : {
					     CaptureMode{ sph::FluidDisplayMode::RawDepth, "raw-depth" },
					     CaptureMode{ sph::FluidDisplayMode::RawThickness, "raw-thickness" },
					     CaptureMode{ sph::FluidDisplayMode::SmoothDepth, "smooth-depth" },
					     CaptureMode{ sph::FluidDisplayMode::SmoothThickness, "smooth-thickness" },
					     CaptureMode{ sph::FluidDisplayMode::ViewNormal, "view-normal" },
					     CaptureMode{ sph::FluidDisplayMode::Reflection, "reflection" },
					     CaptureMode{ sph::FluidDisplayMode::Refraction, "refraction" },
					     CaptureMode{ sph::FluidDisplayMode::Transmission, "transmission" },
					     CaptureMode{ sph::FluidDisplayMode::ScreenSpaceDepth, "composite" } }) {
					renderer.present(capture.mode, 12.0f, 1.5f, material,
					                 inverse_projection, projection, glm::mat4(1.0f));
					capturePng(options.capturePrefix + '-' + capture.suffix + ".png",
					           options.width, options.height);
				}
			}
			std::size_t covered_pixels = 0u;
			if (options.mode == "composite") {
				renderer.refreshThicknessStats(true);
				covered_pixels = renderer.thicknessStats().coveredPixelCount;
			}
			std::vector<GLuint> query_names(options.frames * 9u);
			glGenQueries(static_cast<GLsizei>(query_names.size()), query_names.data());
			for (std::size_t frame = 0u; frame < options.frames; ++frame) {
				std::array<GLuint, 9u> queries{};
				std::copy_n(query_names.begin() + static_cast<std::ptrdiff_t>(frame * 9u),
				            9u, queries.begin());
				render_frame(&queries);
			}
			glFinish();
			for (std::size_t frame = 0u; frame < options.frames; ++frame) {
				std::array<GLuint64, 9u> timestamp{};
				for (std::size_t index = 0u; index < timestamp.size(); ++index)
					glGetQueryObjectui64v(query_names[frame * 9u + index],
					                      GL_QUERY_RESULT, &timestamp[index]);
				Sample sample;
				sample.run = run;
				sample.frame = frame;
				sample.coveredPixels = covered_pixels;
				sample.simulation = milliseconds(timestamp[0], timestamp[1]);
				sample.scene = milliseconds(timestamp[1], timestamp[2]);
				sample.depth = milliseconds(timestamp[2], timestamp[3]);
				sample.thickness = milliseconds(timestamp[3], timestamp[4]);
				sample.smoothDepth = milliseconds(timestamp[4], timestamp[5]);
				sample.smoothThickness = milliseconds(timestamp[5], timestamp[6]);
				sample.normal = milliseconds(timestamp[6], timestamp[7]);
				sample.composite = milliseconds(timestamp[7], timestamp[8]);
				sample.wholeRender = milliseconds(timestamp[1], timestamp[8]);
				sample.endToEnd = milliseconds(timestamp[0], timestamp[8]);
				samples.push_back(sample);
			}
			glDeleteQueries(static_cast<GLsizei>(query_names.size()), query_names.data());
			point_renderer.shutdown();
			renderer.shutdown();
			if (frozen_buffer != 0u) glDeleteBuffers(1, &frozen_buffer);
		}

		std::ofstream csv(options.csv, std::ios::trunc);
		if (!csv) throw std::runtime_error("Could not open benchmark CSV output.");
		csv << "schema_version,scene,backend,render_mode,build_type,gpu_vendor,gpu_renderer,driver,commit,width,height,particles,particle_spacing_m,warmup_frames,measured_frames,run_count,run_id,frame_id,texture_formats,thickness_resolution,filter_type,filter_world_radius_m,max_filter_radius_px,filter_iterations,covered_pixels,reserved_target_bytes,simulation_ms,scene_ms,depth_ms,thickness_ms,smooth_depth_ms,smooth_thickness_ms,normal_ms,composite_ms,whole_render_ms,end_to_end_ms\n";
		csv << std::setprecision(9);
		for (Sample const& sample : samples) {
			csv << "1," << quote("stable-lattice") << ',' << quote(options.backend) << ','
			    << quote(options.mode) << ',' << quote(buildType()) << ','
			    << quote(glString(GL_VENDOR)) << ',' << quote(glString(GL_RENDERER)) << ','
			    << quote(glString(GL_VERSION)) << ',' << quote(options.commit) << ','
			    << options.width << ',' << options.height << ',' << options.particles << ','
			    << particle_spacing << ','
			    << options.warmup << ',' << options.frames << ',' << options.runs << ','
			    << sample.run << ',' << sample.frame << ','
			    << quote("Scene=RGBA16F+D32F;Depth=R32F;Thickness=R16F;Normal=RGB16F") << ','
			    << quote("full") << ',' << quote("separable_bilateral") << ','
			    << smoothing.worldRadiusMetres << ',' << smoothing.maximumRadiusPixels << ','
			    << smoothing.iterations << ',' << sample.coveredPixels << ','
			    << static_cast<std::size_t>(options.width) *
			       static_cast<std::size_t>(options.height) * 40u << ','
			    << sample.simulation << ',' << sample.scene << ',' << sample.depth << ','
			    << sample.thickness << ',' << sample.smoothDepth << ','
			    << sample.smoothThickness << ',' << sample.normal << ','
			    << sample.composite << ',' << sample.wholeRender << ','
			    << sample.endToEnd << '\n';
		}

		std::vector<double> whole;
		std::vector<double> end_to_end;
		for (Sample const& sample : samples) {
			whole.push_back(sample.wholeRender);
			end_to_end.push_back(sample.endToEnd);
		}
		std::cout << "M7 render benchmark: " << glString(GL_RENDERER) << " | "
		          << options.backend << " | " << options.mode << " | "
		          << options.width << 'x' << options.height << " | particles="
		          << options.particles << " | iterations=" << options.iterations << '\n';
		std::cout << std::fixed << std::setprecision(4)
		          << "whole_render median=" << percentile(whole, 0.5)
		          << " ms p95=" << percentile(whole, 0.95)
		          << " ms | end_to_end median=" << percentile(end_to_end, 0.5)
		          << " ms p95=" << percentile(end_to_end, 0.95) << " ms\n";
		std::cout << "Raw samples: " << options.csv << '\n';
		glDeleteVertexArrays(1, &benchmark_scene_vertex_array);
		glfwDestroyWindow(window);
		glfwTerminate();
		return 0;
	} catch (std::exception const& error) {
		std::cerr << "M7 render benchmark failed: " << error.what() << '\n';
		glfwTerminate();
		return 1;
	}
}

// Do not use intrinsic functions, which allows using constexpr on GLM functions.
#define GLM_FORCE_PURE 1

#include "assignment1.hpp"

#include "config.hpp"
#include "core/Bonobo.h"
#include "core/Log.h"
#include "core/LogView.h"
#include "core/ShaderProgramManager.hpp"
#include "core/helpers.hpp"
#include "core/opengl.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr std::uint32_t primitive_sphere = 0u;
	constexpr std::uint32_t primitive_triangle = 1u;
	constexpr std::size_t maximum_leaf_size = 4u;

	struct alignas(16) GpuMaterial
	{
		// xyz = linear diffuse colour, w = reflectivity
		glm::vec4 colour_reflectivity{ 0.0f };
		// x = transparency, y = index of refraction
		glm::vec4 optical{ 0.0f, 1.0f, 0.0f, 0.0f };
	};

	struct alignas(16) GpuPrimitive
	{
		// Sphere: p0.xyz=center, p0.w=radius.
		// Triangle: p0.xyz, p1.xyz and p2.xyz are the three vertices.
		glm::vec4 p0{ 0.0f };
		glm::vec4 p1{ 0.0f };
		glm::vec4 p2{ 0.0f };
		// x = primitive type, y = material index.
		glm::uvec4 metadata{ 0u };
	};

	struct alignas(16) GpuBvhNode
	{
		glm::vec4 minimum{ 0.0f };
		glm::vec4 maximum{ 0.0f };
		// Internal: x/y = children, w = 0. Leaf: z = first index, w = count.
		glm::uvec4 data{ 0u };
	};

	static_assert(sizeof(GpuMaterial) == 32u, "GpuMaterial must match std430 layout");
	static_assert(sizeof(GpuPrimitive) == 64u, "GpuPrimitive must match std430 layout");
	static_assert(sizeof(GpuBvhNode) == 48u, "GpuBvhNode must match std430 layout");

	struct Bounds
	{
		glm::vec3 minimum{ FLT_MAX };
		glm::vec3 maximum{ -FLT_MAX };

		void expand(glm::vec3 const& point)
		{
			minimum = glm::min(minimum, point);
			maximum = glm::max(maximum, point);
		}

		void expand(Bounds const& bounds)
		{
			expand(bounds.minimum);
			expand(bounds.maximum);
		}

		glm::vec3 centroid() const { return 0.5f * (minimum + maximum); }
		glm::vec3 extent() const { return maximum - minimum; }
	};

	struct SceneData
	{
		std::string name;
		std::vector<GpuMaterial> materials;
		std::vector<GpuPrimitive> primitives;
		std::vector<Bounds> primitive_bounds;
		std::vector<GpuBvhNode> bvh_nodes;
		std::vector<std::uint32_t> primitive_indices;

		std::uint32_t addMaterial(glm::vec3 const& colour,
		                          float reflectivity = 0.0f,
		                          float transparency = 0.0f,
		                          float refractive_index = 1.0f)
		{
			GpuMaterial material;
			material.colour_reflectivity = glm::vec4(colour, glm::clamp(reflectivity, 0.0f, 1.0f));
			material.optical = glm::vec4(glm::clamp(transparency, 0.0f, 1.0f),
			                             std::max(refractive_index, 1.0e-4f), 0.0f, 0.0f);
			materials.push_back(material);
			return static_cast<std::uint32_t>(materials.size() - 1u);
		}

		void addSphere(glm::vec3 const& centre, float radius, std::uint32_t material_index)
		{
			GpuPrimitive primitive;
			primitive.p0 = glm::vec4(centre, radius);
			primitive.metadata = glm::uvec4(primitive_sphere, material_index, 0u, 0u);
			primitives.push_back(primitive);

			Bounds bounds;
			bounds.expand(centre - glm::vec3(radius));
			bounds.expand(centre + glm::vec3(radius));
			primitive_bounds.push_back(bounds);
		}

		void addTriangle(glm::vec3 const& v0, glm::vec3 const& v1, glm::vec3 const& v2,
		                 std::uint32_t material_index)
		{
			GpuPrimitive primitive;
			primitive.p0 = glm::vec4(v0, 0.0f);
			primitive.p1 = glm::vec4(v1, 0.0f);
			primitive.p2 = glm::vec4(v2, 0.0f);
			primitive.metadata = glm::uvec4(primitive_triangle, material_index, 0u, 0u);
			primitives.push_back(primitive);

			Bounds bounds;
			bounds.expand(v0);
			bounds.expand(v1);
			bounds.expand(v2);
			bounds.minimum -= glm::vec3(1.0e-5f);
			bounds.maximum += glm::vec3(1.0e-5f);
			primitive_bounds.push_back(bounds);
		}

		std::uint32_t buildBvhRecursive(std::size_t begin, std::size_t end)
		{
			std::uint32_t const node_index = static_cast<std::uint32_t>(bvh_nodes.size());
			bvh_nodes.emplace_back();

			Bounds node_bounds;
			Bounds centroid_bounds;
			for (std::size_t i = begin; i < end; ++i) {
				std::uint32_t const primitive_index = primitive_indices[i];
				node_bounds.expand(primitive_bounds[primitive_index]);
				centroid_bounds.expand(primitive_bounds[primitive_index].centroid());
			}

			std::size_t const count = end - begin;
			if (count <= maximum_leaf_size) {
				GpuBvhNode& node = bvh_nodes[node_index];
				node.minimum = glm::vec4(node_bounds.minimum, 0.0f);
				node.maximum = glm::vec4(node_bounds.maximum, 0.0f);
				node.data = glm::uvec4(0u, 0u, static_cast<std::uint32_t>(begin),
				                       static_cast<std::uint32_t>(count));
				return node_index;
			}

			glm::vec3 const centroid_extent = centroid_bounds.extent();
			std::uint32_t axis = 0u;
			if (centroid_extent.y > centroid_extent.x)
				axis = 1u;
			if (centroid_extent.z > centroid_extent[axis])
				axis = 2u;

			std::size_t const middle = begin + count / 2u;
			std::nth_element(primitive_indices.begin() + static_cast<std::ptrdiff_t>(begin),
			                 primitive_indices.begin() + static_cast<std::ptrdiff_t>(middle),
			                 primitive_indices.begin() + static_cast<std::ptrdiff_t>(end),
			                 [&](std::uint32_t left, std::uint32_t right) {
				                 return primitive_bounds[left].centroid()[axis]
				                      < primitive_bounds[right].centroid()[axis];
			                 });

			std::uint32_t const left = buildBvhRecursive(begin, middle);
			std::uint32_t const right = buildBvhRecursive(middle, end);

			GpuBvhNode& node = bvh_nodes[node_index];
			node.minimum = glm::vec4(node_bounds.minimum, 0.0f);
			node.maximum = glm::vec4(node_bounds.maximum, 0.0f);
			node.data = glm::uvec4(left, right, 0u, 0u);
			return node_index;
		}

		void buildBvh()
		{
			bvh_nodes.clear();
			primitive_indices.resize(primitives.size());
			std::iota(primitive_indices.begin(), primitive_indices.end(), 0u);
			if (!primitives.empty()) {
				bvh_nodes.reserve(2u * primitives.size());
				buildBvhRecursive(0u, primitives.size());
			}
		}
	};

	struct GpuSceneBuffers
	{
		std::array<GLuint, 4u> buffers{ { 0u, 0u, 0u, 0u } };

		void destroy()
		{
			glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
			buffers = { { 0u, 0u, 0u, 0u } };
		}

		template <typename T>
		void uploadOne(GLuint binding, std::vector<T> const& values)
		{
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[binding]);
			glBufferData(GL_SHADER_STORAGE_BUFFER,
			             static_cast<GLsizeiptr>(values.size() * sizeof(T)),
			             values.empty() ? nullptr : values.data(), GL_STATIC_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffers[binding]);
		}

		void upload(SceneData const& scene)
		{
			destroy();
			glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
			uploadOne(0u, scene.primitives);
			uploadOne(1u, scene.materials);
			uploadOne(2u, scene.bvh_nodes);
			uploadOne(3u, scene.primitive_indices);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
		}

		void bind() const
		{
			for (GLuint binding = 0u; binding < buffers.size(); ++binding)
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffers[binding]);
		}
	};

	SceneData makeLabScene()
	{
		SceneData scene;
		scene.name = "Lab scene";

		std::uint32_t const white = scene.addMaterial(glm::vec3(0.9f));
		std::uint32_t const green = scene.addMaterial(glm::vec3(0.1f, 0.6f, 0.1f));
		std::uint32_t const red = scene.addMaterial(glm::vec3(1.0f, 0.1f, 0.1f));
		std::uint32_t const blue = scene.addMaterial(glm::vec3(0.0f, 0.2f, 0.9f));
		std::uint32_t const yellow = scene.addMaterial(glm::vec3(1.0f, 0.6f, 0.1f), 0.2f);
		std::uint32_t const purple = scene.addMaterial(glm::vec3(0.7f, 0.2f, 0.8f), 0.55f);
		std::uint32_t const mirror = scene.addMaterial(glm::vec3(0.9f), 0.8f);
		std::uint32_t const glass = scene.addMaterial(glm::vec3(1.0f), 0.1f, 0.8f, 1.3f);
		std::uint32_t const reflective_glass = scene.addMaterial(glm::vec3(0.9f, 0.95f, 1.0f), 0.35f, 0.55f, 1.5f);

		scene.addSphere(glm::vec3(-7.0f, 3.0f, -20.0f), 3.0f, green);
		scene.addSphere(glm::vec3(0.0f, 3.0f, -20.0f), 3.0f, blue);
		scene.addSphere(glm::vec3(7.0f, 3.0f, -20.0f), 3.0f, red);

		auto const quad = [&](glm::vec3 const& a, glm::vec3 const& b,
		                      glm::vec3 const& c, glm::vec3 const& d,
		                      std::uint32_t material) {
			scene.addTriangle(a, b, c, material);
			scene.addTriangle(a, c, d, material);
		};

		quad(glm::vec3(-20.0f, 0.0f, 50.0f), glm::vec3(20.0f, 0.0f, 50.0f),
		     glm::vec3(20.0f, 0.0f, -50.0f), glm::vec3(-20.0f, 0.0f, -50.0f), white);
		quad(glm::vec3(-20.0f, 0.0f, -50.0f), glm::vec3(20.0f, 0.0f, -50.0f),
		     glm::vec3(20.0f, 40.0f, -50.0f), glm::vec3(-20.0f, 40.0f, -50.0f), white);
		scene.addTriangle(glm::vec3(-20.0f, 40.0f, 50.0f), glm::vec3(-20.0f, 40.0f, -50.0f),
		                  glm::vec3(20.0f, 40.0f, 50.0f), white);
		scene.addTriangle(glm::vec3(20.0f, 40.0f, 50.0f), glm::vec3(-20.0f, 40.0f, -50.0f),
		                  glm::vec3(20.0f, 40.0f, -50.0f), white);
		scene.addTriangle(glm::vec3(-20.0f, 0.0f, 50.0f), glm::vec3(-20.0f, 40.0f, -50.0f),
		                  glm::vec3(-20.0f, 40.0f, 50.0f), red);
		scene.addTriangle(glm::vec3(-20.0f, 0.0f, 50.0f), glm::vec3(-20.0f, 0.0f, -50.0f),
		                  glm::vec3(-20.0f, 40.0f, -50.0f), red);
		scene.addTriangle(glm::vec3(20.0f, 0.0f, 50.0f), glm::vec3(20.0f, 40.0f, -50.0f),
		                  glm::vec3(20.0f, 40.0f, 50.0f), green);
		scene.addTriangle(glm::vec3(20.0f, 0.0f, 50.0f), glm::vec3(20.0f, 0.0f, -50.0f),
		                  glm::vec3(20.0f, 40.0f, -50.0f), green);

		scene.addSphere(glm::vec3(7.0f, 3.0f, 0.0f), 3.0f, yellow);
		scene.addSphere(glm::vec3(9.0f, 10.0f, 0.0f), 3.0f, yellow);
		scene.addSphere(glm::vec3(2.0f, 2.0f, -8.0f), 2.0f, purple);
		scene.addSphere(glm::vec3(0.0f, 8.0f, -12.0f), 2.5f, mirror);
		scene.addSphere(glm::vec3(-7.0f, 3.0f, 0.0f), 3.0f, glass);
		scene.addSphere(glm::vec3(-9.0f, 10.0f, 0.0f), 3.0f, reflective_glass);

		scene.buildBvh();
		return scene;
	}

	bool loadModelScene(std::string const& filename, SceneData& scene)
	{
		Assimp::Importer importer;
		aiScene const* imported = importer.ReadFile(filename,
			aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices);
		if (imported == nullptr || !imported->HasMeshes()) {
			LogError("Assignment 1 could not load model '%s': %s", filename.c_str(), importer.GetErrorString());
			return false;
		}

		Bounds source_bounds;
		bool has_vertices = false;
		for (unsigned int mesh_index = 0u; mesh_index < imported->mNumMeshes; ++mesh_index) {
			aiMesh const* mesh = imported->mMeshes[mesh_index];
			for (unsigned int vertex = 0u; vertex < mesh->mNumVertices; ++vertex) {
				aiVector3D const& p = mesh->mVertices[vertex];
				source_bounds.expand(glm::vec3(p.x, p.y, p.z));
				has_vertices = true;
			}
		}

		float const maximum_extent = std::max(source_bounds.extent().x,
			std::max(source_bounds.extent().y, source_bounds.extent().z));
		if (!has_vertices || !(maximum_extent > 0.0f) || !std::isfinite(maximum_extent)) {
			LogError("Assignment 1 model has degenerate bounds: '%s'", filename.c_str());
			return false;
		}

		scene = SceneData{};
		scene.name = "OBJ model";
		glm::vec3 const source_centre = source_bounds.centroid();
		glm::vec3 const target_centre(0.0f, 10.0f, -5.0f);
		float const scale = 18.0f / maximum_extent;

		std::vector<std::uint32_t> material_indices(imported->mNumMaterials, 0u);
		for (unsigned int material_index = 0u; material_index < imported->mNumMaterials; ++material_index) {
			aiMaterial const* source = imported->mMaterials[material_index];
			aiColor3D diffuse(0.9f, 0.9f, 0.9f);
			float opacity = 1.0f;
			float refractive_index = 1.0f;
			source->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
			source->Get(AI_MATKEY_OPACITY, opacity);
			source->Get(AI_MATKEY_REFRACTI, refractive_index);
			material_indices[material_index] = scene.addMaterial(
				glm::vec3(diffuse.r, diffuse.g, diffuse.b), 0.0f,
				1.0f - glm::clamp(opacity, 0.0f, 1.0f), refractive_index);
		}
		if (scene.materials.empty())
			material_indices.push_back(scene.addMaterial(glm::vec3(0.9f)));

		for (unsigned int mesh_index = 0u; mesh_index < imported->mNumMeshes; ++mesh_index) {
			aiMesh const* mesh = imported->mMeshes[mesh_index];
			std::uint32_t const material_index = mesh->mMaterialIndex < material_indices.size()
				? material_indices[mesh->mMaterialIndex] : 0u;
			for (unsigned int face_index = 0u; face_index < mesh->mNumFaces; ++face_index) {
				aiFace const& face = mesh->mFaces[face_index];
				if (face.mNumIndices != 3u)
					continue;

				glm::vec3 vertices[3];
				for (unsigned int corner = 0u; corner < 3u; ++corner) {
					aiVector3D const& p = mesh->mVertices[face.mIndices[corner]];
					vertices[corner] = (glm::vec3(p.x, p.y, p.z) - source_centre) * scale + target_centre;
				}
				scene.addTriangle(vertices[0], vertices[1], vertices[2], material_index);
			}
		}

		if (scene.primitives.empty()) {
			LogError("Assignment 1 model contains no triangles: '%s'", filename.c_str());
			return false;
		}

		scene.buildBvh();
		LogInfo("Assignment 1 loaded %zu triangles and built %zu BVH nodes from '%s'.",
		        scene.primitives.size(), scene.bvh_nodes.size(), filename.c_str());
		return true;
	}

	GLuint createOutputTexture(int width, int height)
	{
		GLuint texture = 0u;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
		glBindTexture(GL_TEXTURE_2D, 0u);
		return texture;
	}

	void setUniform(GLuint program, char const* name, glm::vec3 const& value)
	{
		glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
	}

	bool cameraTransformChanged(glm::mat4 const& before, glm::mat4 const& after)
	{
		for (glm::length_t column = 0; column < 4; ++column) {
			for (glm::length_t row = 0; row < 4; ++row) {
				if (std::abs(before[column][row] - after[column][row]) > 1.0e-6f)
					return true;
			}
		}
		return false;
	}

	void resetCamera(FPSCameraf& camera)
	{
		camera.mWorld.SetTranslate(glm::vec3(0.0f, 10.0f, 30.0f));
		camera.mWorld.LookAt(glm::vec3(0.0f, 10.0f, -5.0f));
	}

	void dispatchRayTracer(GLuint program, GLuint output_texture, GpuSceneBuffers const& scene_buffers,
	                       int width, int height, int maximum_depth, int samples_per_axis,
	                       glm::vec3 const& eye, glm::vec3 const& forward,
	                       glm::vec3 const& right, glm::vec3 const& up, float vertical_fov)
	{
		float const aspect = static_cast<float>(width) / static_cast<float>(height);
		float const vertical_extent = std::tan(0.5f * vertical_fov);
		glm::vec2 const image_extent(vertical_extent * aspect, vertical_extent);

		glUseProgram(program);
		setUniform(program, "uEye", eye);
		setUniform(program, "uForward", forward);
		setUniform(program, "uRight", right);
		setUniform(program, "uUp", up);
		setUniform(program, "uLightPosition", glm::vec3(0.0f, 30.0f, -5.0f));
		glUniform2f(glGetUniformLocation(program, "uImageExtent"), image_extent.x, image_extent.y);
		glUniform2i(glGetUniformLocation(program, "uResolution"), width, height);
		glUniform1i(glGetUniformLocation(program, "uMaximumDepth"), maximum_depth);
		glUniform1i(glGetUniformLocation(program, "uSamplesPerAxis"), samples_per_axis);

		scene_buffers.bind();
		glBindImageTexture(0u, output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(static_cast<GLuint>((width + 7) / 8),
		                  static_cast<GLuint>((height + 7) / 8), 1u);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
		glUseProgram(0u);
	}
} // namespace

edan35::Assignment1::Assignment1(WindowManager& window_manager, std::string model_path) :
	mCamera(glm::radians(52.0f),
	        static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
	        0.01f, 1000.0f),
	mInputHandler(), mWindowManager(window_manager), mWindow(nullptr), mModelPath(std::move(model_path))
{
	WindowManager::WindowDatum window_datum{
		mInputHandler, mCamera, config::resolution_x, config::resolution_y, 0, 0, 0, 0
	};
	mWindow = mWindowManager.CreateGLFWWindow("EDAN35: Assignment 1 - GPU Ray Tracing",
	                                          window_datum, 1u, false, true);
	if (mWindow == nullptr)
		throw std::runtime_error("Failed to create the Assignment 1 OpenGL window.");

	bonobo::init();
}

edan35::Assignment1::~Assignment1()
{
	bonobo::deinit();
}

void edan35::Assignment1::run()
{
	if (!GLAD_GL_VERSION_4_3 && !GLAD_GL_ARB_compute_shader) {
		LogError("EDAN35 Assignment 1 needs OpenGL compute shader support (OpenGL 4.3 or newer).");
		return;
	}

	SceneData lab_scene = makeLabScene();
	SceneData model_scene;
	bool has_model_scene = !mModelPath.empty() && loadModelScene(mModelPath, model_scene);
	int selected_scene = 0;

	LogInfo("Assignment 1 lab scene: %zu primitives, %zu materials, %zu BVH nodes.",
	        lab_scene.primitives.size(), lab_scene.materials.size(), lab_scene.bvh_nodes.size());

	resetCamera(mCamera);
	mCamera.mMouseSensitivity = glm::vec2(0.003f);
	mCamera.mMovementSpeed = glm::vec3(3.0f);

	ShaderProgramManager program_manager;
	GLuint ray_tracing_program = 0u;
	program_manager.CreateAndRegisterComputeProgram(
		"Assignment 1 GPU ray tracer", "EDAN35/assignment1_raytrace.comp", ray_tracing_program);
	GLuint present_program = 0u;
	program_manager.CreateAndRegisterProgram(
		"Assignment 1 presentation",
		{ { ShaderType::vertex, "EDAN35/assignment1_present.vert" },
		  { ShaderType::fragment, "EDAN35/assignment1_present.frag" } },
		present_program);
	if (ray_tracing_program == 0u || present_program == 0u) {
		LogError("Assignment 1 shader creation failed.");
		return;
	}

	GpuSceneBuffers scene_buffers;
	SceneData const* active_scene = &lab_scene;
	scene_buffers.upload(*active_scene);

	GLuint fullscreen_vao = 0u;
	glGenVertexArrays(1, &fullscreen_vao);
	GLuint elapsed_query = 0u;
	glGenQueries(1, &elapsed_query);

	int framebuffer_width = 0;
	int framebuffer_height = 0;
	glfwGetFramebufferSize(mWindow, &framebuffer_width, &framebuffer_height);
	GLuint output_texture = createOutputTexture(framebuffer_width, framebuffer_height);

	int maximum_depth = 3;
	int samples_per_axis = 3;
	bool trace_is_dirty = true;
	bool show_gui = true;
	bool show_logs = false;
	bool shader_reload_failed = false;
	bool camera_was_moving = false;
	double gpu_milliseconds = 0.0;
	auto last_time = std::chrono::high_resolution_clock::now();

	while (!glfwWindowShouldClose(mWindow)) {
		auto const now = std::chrono::high_resolution_clock::now();
		auto const delta_time = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time);
		last_time = now;

		auto& io = ImGui::GetIO();
		mInputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
		glfwPollEvents();
		mInputHandler.Advance();
		glm::mat4 const camera_before_update = mCamera.GetViewToWorldMatrix();
		mCamera.Update(delta_time, mInputHandler);
		bool const camera_moved_this_frame = cameraTransformChanged(
			camera_before_update, mCamera.GetViewToWorldMatrix());
		if (camera_moved_this_frame) {
			trace_is_dirty = true;
			camera_was_moving = true;
		} else if (camera_was_moving) {
			// Replace the low-cost interactive preview with the selected quality.
			trace_is_dirty = true;
			camera_was_moving = false;
		}

		if (mInputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
			show_gui = !show_gui;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
			show_logs = !show_logs;
		if (mInputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED) {
			shader_reload_failed = !program_manager.ReloadAllPrograms();
			trace_is_dirty = !shader_reload_failed;
		}

		int new_width = 0;
		int new_height = 0;
		glfwGetFramebufferSize(mWindow, &new_width, &new_height);
		if (new_width <= 0 || new_height <= 0)
			continue;
		if (new_width != framebuffer_width || new_height != framebuffer_height) {
			framebuffer_width = new_width;
			framebuffer_height = new_height;
			glDeleteTextures(1, &output_texture);
			output_texture = createOutputTexture(framebuffer_width, framebuffer_height);
			trace_is_dirty = true;
		}

		mWindowManager.NewImGuiFrame();
		bool opened = ImGui::Begin("GPU Ray Tracer", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		if (opened) {
			ImGui::Text("Backend: OpenGL 4.6 Compute Shader");
			ImGui::Text("Scene: %s", active_scene->name.c_str());
			ImGui::Text("Primitives: %zu", active_scene->primitives.size());
			ImGui::Text("BVH nodes: %zu", active_scene->bvh_nodes.size());
			ImGui::Text("Last GPU trace: %.3f ms", gpu_milliseconds);
			glm::vec3 const camera_position = mCamera.mWorld.GetTranslation();
			ImGui::Text("Camera: %.2f, %.2f, %.2f",
			            camera_position.x, camera_position.y, camera_position.z);
			ImGui::Separator();

			if (ImGui::Button("Load OBJ...")) {
				char const* filters[] = { "*.obj" };
				char const* selected_path = tinyfd_openFileDialog(
					"Load an Assignment 1 scene", mModelPath.empty() ? "" : mModelPath.c_str(),
					1, filters, "Wavefront OBJ", 0);
				if (selected_path != nullptr) {
					SceneData loaded_scene;
					if (loadModelScene(selected_path, loaded_scene)) {
						model_scene = std::move(loaded_scene);
						mModelPath = selected_path;
						has_model_scene = true;
						selected_scene = 1;
						active_scene = &model_scene;
						scene_buffers.upload(*active_scene);
						trace_is_dirty = true;
					}
				}
			}
			if (has_model_scene)
				ImGui::TextWrapped("%s", mModelPath.c_str());
			ImGui::Separator();

			int requested_scene = selected_scene;
			ImGui::RadioButton("Lab scene", &requested_scene, 0);
			if (has_model_scene)
				ImGui::RadioButton("OBJ model", &requested_scene, 1);
			else
				ImGui::TextDisabled("OBJ model unavailable: see log");
			if (requested_scene != selected_scene) {
				selected_scene = requested_scene;
				active_scene = selected_scene == 0 ? &lab_scene : &model_scene;
				scene_buffers.upload(*active_scene);
				trace_is_dirty = true;
			}

			trace_is_dirty |= ImGui::SliderInt("Maximum depth", &maximum_depth, 0, 5);
			trace_is_dirty |= ImGui::SliderInt("Samples per axis", &samples_per_axis, 1, 3);
			if (ImGui::Button("Trace again"))
				trace_is_dirty = true;
			ImGui::SameLine();
			if (ImGui::Button("Reset camera")) {
				resetCamera(mCamera);
				trace_is_dirty = true;
			}
			ImGui::TextDisabled("WASD: move  Q/E: down/up  Left mouse: look");
			ImGui::TextDisabled("Ctrl: slow  Shift: fast");
			ImGui::TextDisabled("R: reload shaders  F2: UI  F3: log");
			if (shader_reload_failed)
				ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f), "Shader reload failed; see log.");
		}
		ImGui::End();

		if (!shader_reload_failed && trace_is_dirty) {
			int const dispatch_depth = camera_moved_this_frame ? std::min(maximum_depth, 1) : maximum_depth;
			int const dispatch_samples = camera_moved_this_frame ? 1 : samples_per_axis;
			glBeginQuery(GL_TIME_ELAPSED, elapsed_query);
			dispatchRayTracer(ray_tracing_program, output_texture, scene_buffers,
			                  framebuffer_width, framebuffer_height, dispatch_depth, dispatch_samples,
			                  mCamera.mWorld.GetTranslation(), glm::normalize(mCamera.mWorld.GetFront()),
			                  glm::normalize(mCamera.mWorld.GetRight()), glm::normalize(mCamera.mWorld.GetUp()),
			                  mCamera.GetFov());
			glEndQuery(GL_TIME_ELAPSED);
			GLuint64 nanoseconds = 0u;
			glGetQueryObjectui64v(elapsed_query, GL_QUERY_RESULT, &nanoseconds);
			gpu_milliseconds = static_cast<double>(nanoseconds) / 1000000.0;
			trace_is_dirty = false;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		glViewport(0, 0, framebuffer_width, framebuffer_height);
		glDisable(GL_DEPTH_TEST);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(present_program);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, output_texture);
		glUniform1i(glGetUniformLocation(present_program, "uImage"), 0);
		glBindVertexArray(fullscreen_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0u);
		glBindTexture(GL_TEXTURE_2D, 0u);
		glUseProgram(0u);

		if (show_logs)
			Log::View::Render();
		mWindowManager.RenderImGuiFrame(show_gui);
		glfwSwapBuffers(mWindow);
	}

	scene_buffers.destroy();
	glDeleteTextures(1, &output_texture);
	glDeleteQueries(1, &elapsed_query);
	glDeleteVertexArrays(1, &fullscreen_vao);
}

int main(int argc, char* argv[])
{
	std::setlocale(LC_ALL, "");

	// A model can be supplied here or selected later through the ImGui control panel.
	std::string const model_path = argc > 1
		? std::string(argv[1])
		: std::string();

	Bonobo framework;
	try {
		edan35::Assignment1 assignment1(framework.GetWindowManager(), model_path);
		assignment1.run();
	} catch (std::runtime_error const& error) {
		LogError(error.what());
	}
	return 0;
}

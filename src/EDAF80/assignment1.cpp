#include "CelestialBody.hpp"
#include "config.hpp"
#include "parametric_shapes.hpp"
#include "core/Bonobo.h"
#include "core/FPSCamera.h"
#include "core/helpers.hpp"
#include "core/node.hpp"
#include "core/ShaderProgramManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <clocale>
#include <cstdlib>
#include <stack>
#include <vector>


namespace
{
	bonobo::mesh_data create_orbit_circle(unsigned int const segment_count)
	{
		std::vector<glm::vec3> vertices;
		vertices.reserve(segment_count);
		for (unsigned int segment = 0u; segment < segment_count; ++segment)
		{
			auto const angle = glm::two_pi<float>() *
			                   static_cast<float>(segment) / static_cast<float>(segment_count);
			vertices.emplace_back(std::cos(angle), 0.0f, std::sin(angle));
		}

		bonobo::mesh_data orbit_circle;
		orbit_circle.vertices_nb = static_cast<GLsizei>(vertices.size());
		orbit_circle.drawing_mode = GL_LINE_LOOP;
		orbit_circle.name = "Orbit circle";

		glGenVertexArrays(1, &orbit_circle.vao);
		glBindVertexArray(orbit_circle.vao);

		glGenBuffers(1, &orbit_circle.bo);
		glBindBuffer(GL_ARRAY_BUFFER, orbit_circle.bo);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)),
		             vertices.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::vertices));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::vertices),
		                      3, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, 0u);
		glBindVertexArray(0u);
		return orbit_circle;
	}
}


int main()
{
	std::setlocale(LC_ALL, "");

	using namespace std::literals::chrono_literals;

	//
	// Set up the framework
	//
	Bonobo framework;

	//
	// Set up the camera
	//
	InputHandler input_handler;
	FPSCameraf camera(0.5f * glm::half_pi<float>(),
	                  static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
	                  0.01f, 1000.0f);
	camera.mWorld.SetTranslate(glm::vec3(0.0f, 4.0f, 20.0f));
	camera.mWorld.LookAt(glm::vec3(0.0f));
	camera.mMouseSensitivity = glm::vec2(0.003f);
	camera.mMovementSpeed = glm::vec3(3.0f); // 3 m/s => 10.8 km/h

	//
	// Create the window
	//
	WindowManager& window_manager = framework.GetWindowManager();
	WindowManager::WindowDatum window_datum{ input_handler, camera, config::resolution_x, config::resolution_y, 0, 0, 0, 0};
	GLFWwindow* window = window_manager.CreateGLFWWindow("EDAF80: Assignment 1", window_datum, config::msaa_rate);
	if (window == nullptr) {
		LogError("Failed to get a window: exiting.");

		return EXIT_FAILURE;
	}

	bonobo::init();

	//
	// Load the sphere geometry
	//
	std::vector<bonobo::mesh_data> const objects = bonobo::loadObjects(config::resources_path("scenes/sphere.obj"));
	if (objects.empty()) {
		LogError("Failed to load the sphere geometry: exiting.");

		bonobo::deinit();

		return EXIT_FAILURE;
	}
	bonobo::mesh_data const& sphere = objects.front();
	auto const saturn_ring_shape = parametric_shapes::createCircleRing(0.675f, 0.45f, 80u, 8u);
	auto const orbit_circle_shape = create_orbit_circle(192u);


	//
	// Create the shader program
	//
	ShaderProgramManager program_manager;
	GLuint celestial_body_shader = 0u;
	program_manager.CreateAndRegisterProgram("Celestial Body",
	                                         { { ShaderType::vertex, "EDAF80/default.vert" },
	                                           { ShaderType::fragment, "EDAF80/default.frag" } },
	                                         celestial_body_shader);
	if (celestial_body_shader == 0u) {
		LogError("Failed to generate the “Celestial Body” shader program: exiting.");

		bonobo::deinit();

		return EXIT_FAILURE;
	}
	GLuint celestial_ring_shader = 0u;
	program_manager.CreateAndRegisterProgram("Celestial Ring",
	                                         { { ShaderType::vertex, "EDAF80/celestial_ring.vert" },
	                                           { ShaderType::fragment, "EDAF80/celestial_ring.frag" } },
	                                         celestial_ring_shader);
	if (celestial_ring_shader == 0u) {
		LogError("Failed to generate the “Celestial Ring” shader program: exiting.");

		bonobo::deinit();

		return EXIT_FAILURE;
	}
	GLuint orbit_shader = 0u;
	program_manager.CreateAndRegisterProgram("Orbit",
	                                         { { ShaderType::vertex, "EDAF80/orbit.vert" },
	                                           { ShaderType::fragment, "EDAF80/orbit.frag" } },
	                                         orbit_shader);
	if (orbit_shader == 0u) {
		LogError("Failed to generate the “Orbit” shader program: exiting.");

		bonobo::deinit();

		return EXIT_FAILURE;
	}


	//
	// Define all the celestial bodies constants.
	//
	glm::vec3 const sun_scale{ 1.0f };
	SpinConfiguration const sun_spin{ glm::radians(0.0f), glm::two_pi<float>() / 6.0f };

	glm::vec3 const mercury_scale{ 0.02f };
	SpinConfiguration const mercury_spin{ glm::radians(-0.0f), glm::two_pi<float>() / 180.0f };
	OrbitConfiguration const mercury_orbit{ 2.0f, glm::radians(-3.4f), glm::two_pi<float>() / 4.0f };

	glm::vec3 const venus_scale{ 0.05f };
	SpinConfiguration const venus_spin{ glm::radians(-2.6f), -glm::two_pi<float>() / 600.0f };
	OrbitConfiguration const venus_orbit{ 3.0f, glm::radians(-3.9f), glm::two_pi<float>() / 12.0f };

	glm::vec3 const earth_scale{ 0.05f };
	SpinConfiguration const earth_spin{ glm::radians(-23.0f), glm::two_pi<float>() / 3.0f };
	OrbitConfiguration const earth_orbit{ 4.0f, glm::radians(-7.2f), glm::two_pi<float>() / 20.0f };

	glm::vec3 const moon_scale{ 0.01f };
	SpinConfiguration const moon_spin{ glm::radians(-6.7f), glm::two_pi<float>() / 90.0f, true };
	OrbitConfiguration const moon_orbit{ 0.2f, glm::radians(29.0f), glm::two_pi<float>() / 1.3f };

	glm::vec3 const mars_scale{ 0.03f };
	SpinConfiguration const mars_spin{ glm::radians(-25.0f), glm::two_pi<float>() / 3.0f };
	OrbitConfiguration const mars_orbit{ 5.0f, glm::radians(-5.7f), glm::two_pi<float>() / 36.0f };

	glm::vec3 const jupiter_scale{ 0.5f };
	SpinConfiguration const jupiter_spin{ glm::radians(-3.1f), glm::two_pi<float>() / 1.0f };
	OrbitConfiguration const jupiter_orbit{ 13.0f, glm::radians(-6.1f), glm::two_pi<float>() / 220.0f };

	glm::vec3 const saturn_scale{ 0.4f };
	SpinConfiguration const saturn_spin{ glm::radians(-27.0f), glm::two_pi<float>() / 1.2f };
	OrbitConfiguration const saturn_orbit{ 16.0f, glm::radians(-5.5f), glm::two_pi<float>() / 400.0f };
	glm::vec2 const saturn_ring_scale{ 1.0f, 1.25f };

	glm::vec3 const uranus_scale{ 0.2f };
	SpinConfiguration const uranus_spin{ glm::radians(-82.0f), -glm::two_pi<float>() / 2.0f };
	OrbitConfiguration const uranus_orbit{ 18.0f, glm::radians(-6.5f), glm::two_pi<float>() / 1680.0f };

	glm::vec3 const neptune_scale{ 0.2f };
	SpinConfiguration const neptune_spin{ glm::radians(-28.0f), glm::two_pi<float>() / 2.0f };
	OrbitConfiguration const neptune_orbit{ 19.0f, glm::radians(-6.4f), glm::two_pi<float>() / 3200.0f };


	//
	// Load all textures.
	//
	GLuint const sun_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_sun.jpg"));
	GLuint const mercury_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_mercury.jpg"));
	GLuint const venus_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_venus_atmosphere.jpg"));
	GLuint const earth_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_earth_daymap.jpg"));
	GLuint const moon_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_moon.jpg"));
	GLuint const mars_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_mars.jpg"));
	GLuint const jupiter_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_jupiter.jpg"));
	GLuint const saturn_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_saturn.jpg"));
	GLuint const saturn_ring_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_saturn_ring_alpha.png"));
	GLuint const uranus_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_uranus.jpg"));
	GLuint const neptune_texture = bonobo::loadTexture2D(config::resources_path("planets/2k_neptune.jpg"));


	//
	// Set up the celestial bodies.
	//
	CelestialBody sun(sphere, &celestial_body_shader, sun_texture);
	sun.set_scale(sun_scale);
	sun.set_spin(sun_spin);

	CelestialBody mercury(sphere, &celestial_body_shader, mercury_texture);
	mercury.set_scale(mercury_scale);
	mercury.set_spin(mercury_spin);
	mercury.set_orbit(mercury_orbit);

	CelestialBody venus(sphere, &celestial_body_shader, venus_texture);
	venus.set_scale(venus_scale);
	venus.set_spin(venus_spin);
	venus.set_orbit(venus_orbit);

	CelestialBody earth(sphere, &celestial_body_shader, earth_texture);
	earth.set_scale(earth_scale);
	earth.set_spin(earth_spin);
	earth.set_orbit(earth_orbit);

	CelestialBody moon(sphere, &celestial_body_shader, moon_texture);
	moon.set_scale(moon_scale);
	moon.set_spin(moon_spin);
	moon.set_orbit(moon_orbit);

	CelestialBody mars(sphere, &celestial_body_shader, mars_texture);
	mars.set_scale(mars_scale);
	mars.set_spin(mars_spin);
	mars.set_orbit(mars_orbit);

	CelestialBody jupiter(sphere, &celestial_body_shader, jupiter_texture);
	jupiter.set_scale(jupiter_scale);
	jupiter.set_spin(jupiter_spin);
	jupiter.set_orbit(jupiter_orbit);

	CelestialBody saturn(sphere, &celestial_body_shader, saturn_texture);
	saturn.set_scale(saturn_scale);
	saturn.set_spin(saturn_spin);
	saturn.set_orbit(saturn_orbit);
	saturn.set_ring(saturn_ring_shape, &celestial_ring_shader,
	                saturn_ring_texture, saturn_ring_scale);

	CelestialBody uranus(sphere, &celestial_body_shader, uranus_texture);
	uranus.set_scale(uranus_scale);
	uranus.set_spin(uranus_spin);
	uranus.set_orbit(uranus_orbit);

	CelestialBody neptune(sphere, &celestial_body_shader, neptune_texture);
	neptune.set_scale(neptune_scale);
	neptune.set_spin(neptune_spin);
	neptune.set_orbit(neptune_orbit);

	earth.add_child(&moon);
	sun.add_child(&mercury);
	sun.add_child(&venus);
	sun.add_child(&earth);
	sun.add_child(&mars);
	sun.add_child(&jupiter);
	sun.add_child(&saturn);
	sun.add_child(&uranus);
	sun.add_child(&neptune);

	Node orbit_node;
	orbit_node.set_geometry(orbit_circle_shape);
	orbit_node.set_program(&orbit_shader);

	char const* const tour_target_names[] = {
		"Sun", "Mercury", "Venus", "Earth", "Moon",
		"Mars", "Jupiter", "Saturn", "Uranus", "Neptune"
	};
	CelestialBody* const tour_targets[] = {
		&sun, &mercury, &venus, &earth, &moon,
		&mars, &jupiter, &saturn, &uranus, &neptune
	};
	OrbitConfiguration const* const target_orbits[] = {
		nullptr, &mercury_orbit, &venus_orbit, &earth_orbit, &moon_orbit,
		&mars_orbit, &jupiter_orbit, &saturn_orbit, &uranus_orbit, &neptune_orbit
	};
	float tour_camera_distances[] = {
		3.0f, 0.12f, 0.2f, 0.2f, 0.06f,
		0.15f, 1.2f, 2.5f, 0.6f, 0.6f
	};
	float const tour_min_camera_distances[] = {
		1.1f, 0.03f, 0.06f, 0.06f, 0.015f,
		0.04f, 0.55f, 0.9f, 0.25f, 0.25f
	};
	constexpr int tour_target_count = static_cast<int>(sizeof(tour_targets) / sizeof(tour_targets[0]));
	glm::mat4 tour_target_transforms[tour_target_count];
	bool tour_target_transform_valid[tour_target_count]{};


	//
	// Define the colour and depth used for clearing.
	//
	glClearDepthf(1.0f);
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glEnable(GL_DEPTH_TEST);


	auto last_time = std::chrono::high_resolution_clock::now();


	bool pause_animation = false;
	bool show_logs = true;
	bool show_gui = true;
	bool show_basis = false;
	bool show_orbits = false;
	bool tour_enabled = false;
	int tour_target_index = 3;
	float tour_yaw = 0.0f;
	float tour_pitch = std::atan(0.35f);
	glm::vec2 tour_last_mouse_position = input_handler.GetMousePosition();
	float time_scale = 1.0f;

	while (!glfwWindowShouldClose(window)) {
		//
		// Compute timings information
		//
		auto const now_time = std::chrono::high_resolution_clock::now();
		auto const delta_time_us = std::chrono::duration_cast<std::chrono::microseconds>(now_time - last_time);
		auto const animation_delta_time_us = !pause_animation ? std::chrono::duration_cast<std::chrono::microseconds>(delta_time_us * time_scale) : 0us;
		last_time = now_time;


		//
		// Process inputs
		//
		glfwPollEvents();

		ImGuiIO const& io = ImGui::GetIO();
		input_handler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
		input_handler.Advance();

		auto const tour_mouse_position = input_handler.GetMousePosition();
		auto const tour_mouse_delta = tour_mouse_position - tour_last_mouse_position;
		tour_last_mouse_position = tour_mouse_position;

		camera.Update(delta_time_us, input_handler, tour_enabled, tour_enabled);

		if (tour_enabled && tour_target_transform_valid[tour_target_index])
		{
			if (!input_handler.IsMouseCapturedByUI() &&
			    (input_handler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT) & PRESSED))
			{
				tour_yaw -= tour_mouse_delta.x * camera.mMouseSensitivity.x;
				tour_pitch += tour_mouse_delta.y * camera.mMouseSensitivity.y;

				auto const pitch_limit = glm::half_pi<float>() - glm::radians(1.0f);
				tour_pitch = std::max(-pitch_limit, std::min(pitch_limit, tour_pitch));
			}

			if (!input_handler.IsMouseCapturedByUI() && io.MouseWheel != 0.0f)
			{
				auto const speed_modifier =
					(input_handler.GetKeycodeState(GLFW_KEY_LEFT_CONTROL) & PRESSED) ? 0.25f :
					(input_handler.GetKeycodeState(GLFW_KEY_LEFT_SHIFT) & PRESSED) ? 4.0f : 1.0f;
				auto& distance = tour_camera_distances[tour_target_index];
				distance *= std::pow(0.85f, io.MouseWheel * speed_modifier);
				distance = std::max(tour_min_camera_distances[tour_target_index],
				                    std::min(100.0f, distance));
			}

			auto const& target_transform = tour_target_transforms[tour_target_index];
			auto const target_position = glm::vec3(target_transform[3]);
			auto const distance = tour_camera_distances[tour_target_index];
			auto const cos_pitch = std::cos(tour_pitch);
			auto const camera_offset = distance * glm::vec3(
				cos_pitch * std::sin(tour_yaw),
				std::sin(tour_pitch),
				cos_pitch * std::cos(tour_yaw));
			auto const camera_position = target_position + camera_offset;

			camera.mWorld.SetTranslate(camera_position);
			camera.mWorld.LookAt(target_position);
		}

		if (input_handler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
			show_logs = !show_logs;
		if (input_handler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
			show_gui = !show_gui;
		if (input_handler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
			window_manager.ToggleFullscreenStatusForWindow(window);


		// Retrieve the actual framebuffer size: for HiDPI monitors,
		// you might end up with a framebuffer larger than what you
		// actually asked for. For example, if you ask for a 1920x1080
		// framebuffer, you might get a 3840x2160 one instead.
		// Also it might change as the user drags the window between
		// monitors with different DPIs, or if the fullscreen status is
		// being toggled.
		int framebuffer_width, framebuffer_height;
		glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
		glViewport(0, 0, framebuffer_width, framebuffer_height);


		//
		// Start a new frame for Dear ImGui
		//
		window_manager.NewImGuiFrame();


		//
		// Clear the screen
		//
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);


		//
		// Traverse the scene graph and render all nodes
		//
		auto const view_projection = camera.GetWorldToClipMatrix();
		struct CelestialBodyRef
		{
			CelestialBody* body;
			glm::mat4 parent_transform;
		};
		std::stack<CelestialBodyRef> bodies_to_render;
		bodies_to_render.push({ &sun, glm::mat4(1.0f) });

		while (!bodies_to_render.empty())
		{
			auto const current = bodies_to_render.top();
			bodies_to_render.pop();
			int current_target_index = -1;
			for (int target_index = 0; target_index < tour_target_count; ++target_index)
			{
				if (tour_targets[target_index] == current.body)
				{
					current_target_index = target_index;
					break;
				}
			}

			if (show_orbits && current_target_index >= 0 && target_orbits[current_target_index] != nullptr)
			{
				auto const& orbit = *target_orbits[current_target_index];
				auto const orbit_tilt = glm::rotate(glm::mat4(1.0f), orbit.inclination,
				                                    glm::vec3(0.0f, 0.0f, 1.0f));
				auto const orbit_scale = glm::scale(glm::mat4(1.0f), glm::vec3(orbit.radius));
				orbit_node.render(view_projection,
				                  current.parent_transform * orbit_tilt * orbit_scale);
			}

			auto const children_transform = current.body->render(
				animation_delta_time_us,
				view_projection,
				current.parent_transform,
				show_basis);

			if (current_target_index >= 0)
			{
				tour_target_transforms[current_target_index] = children_transform;
				tour_target_transform_valid[current_target_index] = true;
			}

			for (auto* child : current.body->get_children())
				bodies_to_render.push({ child, children_transform });
		}


		//
		// Add controls to the scene.
		//
		bool const opened = ImGui::Begin("Scene controls", nullptr, ImGuiWindowFlags_None);
		if (opened)
		{
			ImGui::Checkbox("Pause the animation", &pause_animation);
			ImGui::SliderFloat("Time scale", &time_scale, 1e-1f, 10.0f);
			ImGui::Separator();
			ImGui::Checkbox("Show basis", &show_basis);
			ImGui::Checkbox("Show orbits", &show_orbits);
			ImGui::Separator();
			ImGui::Checkbox("Interplanetary tour", &tour_enabled);
			ImGui::Combo("Tour target", &tour_target_index,
			             tour_target_names, tour_target_count);
			if (tour_enabled)
				ImGui::TextUnformatted("LMB drag: orbit | Wheel: zoom | Shift: faster");
		}
		ImGui::End();


		//
		// Display Dear ImGui windows
		//
		if (show_logs)
			Log::View::Render();
		window_manager.RenderImGuiFrame(show_gui);


		//
		// Queue the computed frame for display on screen
		//
		glfwSwapBuffers(window);
	}

	glDeleteTextures(1, &neptune_texture);
	glDeleteTextures(1, &uranus_texture);
	glDeleteTextures(1, &saturn_ring_texture);
	glDeleteTextures(1, &saturn_texture);
	glDeleteTextures(1, &jupiter_texture);
	glDeleteTextures(1, &mars_texture);
	glDeleteTextures(1, &moon_texture);
	glDeleteTextures(1, &earth_texture);
	glDeleteTextures(1, &venus_texture);
	glDeleteTextures(1, &mercury_texture);
	glDeleteTextures(1, &sun_texture);
	glDeleteBuffers(1, &orbit_circle_shape.bo);
	glDeleteVertexArrays(1, &orbit_circle_shape.vao);

	bonobo::deinit();

	return EXIT_SUCCESS;
}

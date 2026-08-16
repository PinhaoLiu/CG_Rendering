#include "parametric_shapes.hpp"
#include "core/Log.h"

#include <glm/glm.hpp>

#include <cassert>
#include <cmath>
#include <vector>

namespace
{
	bonobo::mesh_data uploadParametricSurface(
		std::vector<glm::vec3> const& vertices,
		std::vector<glm::vec3> const& normals,
		std::vector<glm::vec3> const& texcoords,
		std::vector<glm::vec3> const& tangents,
		std::vector<glm::vec3> const& binormals,
		std::vector<glm::uvec3> const& index_sets)
	{
		bonobo::mesh_data data;
		glGenVertexArrays(1, &data.vao);
		assert(data.vao != 0u);
		glBindVertexArray(data.vao);

		auto const vertices_offset = 0u;
		auto const vertices_size = static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3));
		auto const normals_offset = vertices_size;
		auto const normals_size = static_cast<GLsizeiptr>(normals.size() * sizeof(glm::vec3));
		auto const texcoords_offset = normals_offset + normals_size;
		auto const texcoords_size = static_cast<GLsizeiptr>(texcoords.size() * sizeof(glm::vec3));
		auto const tangents_offset = texcoords_offset + texcoords_size;
		auto const tangents_size = static_cast<GLsizeiptr>(tangents.size() * sizeof(glm::vec3));
		auto const binormals_offset = tangents_offset + tangents_size;
		auto const binormals_size = static_cast<GLsizeiptr>(binormals.size() * sizeof(glm::vec3));
		auto const bo_size = static_cast<GLsizeiptr>(vertices_size
		                                            + normals_size
		                                            + texcoords_size
		                                            + tangents_size
		                                            + binormals_size);

		glGenBuffers(1, &data.bo);
		assert(data.bo != 0u);
		glBindBuffer(GL_ARRAY_BUFFER, data.bo);
		glBufferData(GL_ARRAY_BUFFER, bo_size, nullptr, GL_STATIC_DRAW);

		glBufferSubData(GL_ARRAY_BUFFER, vertices_offset, vertices_size, vertices.data());
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::vertices));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::vertices),
		                      3, GL_FLOAT, GL_FALSE, 0,
		                      reinterpret_cast<GLvoid const*>(vertices_offset));

		glBufferSubData(GL_ARRAY_BUFFER, normals_offset, normals_size, normals.data());
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::normals));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::normals),
		                      3, GL_FLOAT, GL_FALSE, 0,
		                      reinterpret_cast<GLvoid const*>(normals_offset));

		glBufferSubData(GL_ARRAY_BUFFER, texcoords_offset, texcoords_size, texcoords.data());
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::texcoords));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::texcoords),
		                      3, GL_FLOAT, GL_FALSE, 0,
		                      reinterpret_cast<GLvoid const*>(texcoords_offset));

		glBufferSubData(GL_ARRAY_BUFFER, tangents_offset, tangents_size, tangents.data());
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::tangents));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::tangents),
		                      3, GL_FLOAT, GL_FALSE, 0,
		                      reinterpret_cast<GLvoid const*>(tangents_offset));

		glBufferSubData(GL_ARRAY_BUFFER, binormals_offset, binormals_size, binormals.data());
		glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::binormals));
		glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::binormals),
		                      3, GL_FLOAT, GL_FALSE, 0,
		                      reinterpret_cast<GLvoid const*>(binormals_offset));

		glBindBuffer(GL_ARRAY_BUFFER, 0u);

		data.indices_nb = static_cast<GLsizei>(index_sets.size() * 3u);
		glGenBuffers(1, &data.ibo);
		assert(data.ibo != 0u);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(index_sets.size() * sizeof(glm::uvec3)),
		             index_sets.data(), GL_STATIC_DRAW);

		glBindVertexArray(0u);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);

		return data;
	}
}

bonobo::mesh_data
parametric_shapes::createQuad(float const width, float const height,
                              unsigned int const horizontal_split_count,
                              unsigned int const vertical_split_count)
{
	// A split count of zero means one edge, one means two edges, etc.
	auto const horizontal_edge_count = horizontal_split_count + 1u;
	auto const vertical_edge_count = vertical_split_count + 1u;
	auto const horizontal_vertex_count = horizontal_edge_count + 1u;
	auto const vertical_vertex_count = vertical_edge_count + 1u;
	auto const vertex_count = static_cast<std::size_t>(horizontal_vertex_count)
	                        * static_cast<std::size_t>(vertical_vertex_count);

	auto vertices = std::vector<glm::vec3>(vertex_count);
	auto normals = std::vector<glm::vec3>(
		vertex_count, glm::vec3(0.0f, 1.0f, 0.0f));
	auto texcoords = std::vector<glm::vec3>(vertex_count);
	auto tangents = std::vector<glm::vec3>(
		vertex_count, glm::vec3(1.0f, 0.0f, 0.0f));
	auto binormals = std::vector<glm::vec3>(
		vertex_count, glm::vec3(0.0f, 0.0f, 1.0f));

	std::size_t vertex_index = 0u;
	for (unsigned int vertical = 0u; vertical < vertical_vertex_count; ++vertical) {
		auto const v = static_cast<float>(vertical)
		             / static_cast<float>(vertical_edge_count);
		for (unsigned int horizontal = 0u; horizontal < horizontal_vertex_count; ++horizontal) {
			auto const u = static_cast<float>(horizontal)
			             / static_cast<float>(horizontal_edge_count);

			// The water surface lies in the x,z-plane. The shader derives the
			// displaced basis analytically, while these base attributes keep the
			// VAO layout consistent with the other parametric surfaces.
			vertices[vertex_index] = glm::vec3(u * width, 0.0f, v * height);
			texcoords[vertex_index] = glm::vec3(u, v, 0.0f);
			++vertex_index;
		}
	}

	auto index_sets = std::vector<glm::uvec3>(
		2u * static_cast<std::size_t>(horizontal_edge_count)
		   * static_cast<std::size_t>(vertical_edge_count));
	std::size_t triangle_index = 0u;
	for (unsigned int vertical = 0u; vertical < vertical_edge_count; ++vertical) {
		for (unsigned int horizontal = 0u; horizontal < horizontal_edge_count; ++horizontal) {
			auto const current = vertical * horizontal_vertex_count + horizontal;
			auto const next_row = current + horizontal_vertex_count;

			// Counter-clockwise winding when viewed from above (+y).
			index_sets[triangle_index++] = glm::uvec3(
				current, next_row, next_row + 1u);
			index_sets[triangle_index++] = glm::uvec3(
				current, next_row + 1u, current + 1u);
		}
	}

	auto data = uploadParametricSurface(
		vertices, normals, texcoords, tangents, binormals, index_sets);
	data.vertices_nb = static_cast<GLsizei>(vertices.size());
	data.name = "tessellated quad";

	return data;
}

bonobo::mesh_data
parametric_shapes::createSphere(float const radius,
                                unsigned int const longitude_split_count,
                                unsigned int const latitude_split_count)
{
	auto const longitude_edge_count = longitude_split_count + 1u;
	auto const latitude_edge_count = latitude_split_count + 1u;
	auto const longitude_vertex_count = longitude_edge_count + 1u;
	auto const latitude_vertex_count = latitude_edge_count + 1u;
	auto const vertex_count = longitude_vertex_count * latitude_vertex_count;

	auto vertices = std::vector<glm::vec3>(vertex_count);
	auto normals = std::vector<glm::vec3>(vertex_count);
	auto texcoords = std::vector<glm::vec3>(vertex_count);
	auto tangents = std::vector<glm::vec3>(vertex_count);
	auto binormals = std::vector<glm::vec3>(vertex_count);

	auto const d_theta = glm::two_pi<float>() / static_cast<float>(longitude_edge_count);
	auto const d_phi = glm::pi<float>() / static_cast<float>(latitude_edge_count);

	std::size_t vertex_index = 0u;
	for (unsigned int longitude = 0u; longitude < longitude_vertex_count; ++longitude) {
		// Reuse theta == 0 at the seam so the first and last positions match exactly.
		auto const theta = longitude == longitude_edge_count
		                 ? 0.0f
		                 : static_cast<float>(longitude) * d_theta;
		auto const sin_theta = std::sin(theta);
		auto const cos_theta = std::cos(theta);

		for (unsigned int latitude = 0u; latitude < latitude_vertex_count; ++latitude) {
			float sin_phi;
			float cos_phi;
			if (latitude == 0u) {
				sin_phi = 0.0f;
				cos_phi = 1.0f;
			}
			else if (latitude == latitude_edge_count) {
				sin_phi = 0.0f;
				cos_phi = -1.0f;
			}
			else {
				auto const phi = static_cast<float>(latitude) * d_phi;
				sin_phi = std::sin(phi);
				cos_phi = std::cos(phi);
			}

			vertices[vertex_index] = radius * glm::vec3(
				sin_theta * sin_phi,
				-cos_phi,
				cos_theta * sin_phi);

			tangents[vertex_index] = glm::normalize(glm::vec3(
				cos_theta, 0.0f, -sin_theta));
			binormals[vertex_index] = glm::normalize(glm::vec3(
				sin_theta * cos_phi,
				sin_phi,
				cos_theta * cos_phi));
			normals[vertex_index] = glm::normalize(glm::cross(
				tangents[vertex_index], binormals[vertex_index]));

			texcoords[vertex_index] = glm::vec3(
				static_cast<float>(longitude) / static_cast<float>(longitude_edge_count),
				static_cast<float>(latitude) / static_cast<float>(latitude_edge_count),
				0.0f);

			++vertex_index;
		}
	}

	auto index_sets = std::vector<glm::uvec3>(
		2u * longitude_edge_count * latitude_edge_count);
	std::size_t index = 0u;
	for (unsigned int longitude = 0u; longitude < longitude_edge_count; ++longitude) {
		for (unsigned int latitude = 0u; latitude < latitude_edge_count; ++latitude) {
			auto const current = latitude_vertex_count * longitude + latitude;
			auto const next_longitude = current + latitude_vertex_count;

			// Counter-clockwise winding when viewed from outside the sphere.
			index_sets[index++] = glm::uvec3(
				current, next_longitude, next_longitude + 1u);
			index_sets[index++] = glm::uvec3(
				current, next_longitude + 1u, current + 1u);
		}
	}

	return uploadParametricSurface(
		vertices, normals, texcoords, tangents, binormals, index_sets);
}

bonobo::mesh_data
parametric_shapes::createTorus(float const major_radius,
                               float const minor_radius,
                               unsigned int const major_split_count,
                               unsigned int const minor_split_count)
{
	auto const major_edge_count = major_split_count + 1u;
	auto const minor_edge_count = minor_split_count + 1u;
	auto const major_vertex_count = major_edge_count + 1u;
	auto const minor_vertex_count = minor_edge_count + 1u;
	auto const vertex_count = major_vertex_count * minor_vertex_count;

	auto vertices = std::vector<glm::vec3>(vertex_count);
	auto normals = std::vector<glm::vec3>(vertex_count);
	auto texcoords = std::vector<glm::vec3>(vertex_count);
	auto tangents = std::vector<glm::vec3>(vertex_count);
	auto binormals = std::vector<glm::vec3>(vertex_count);

	auto const d_phi = glm::two_pi<float>() / static_cast<float>(major_edge_count);
	auto const d_theta = glm::two_pi<float>() / static_cast<float>(minor_edge_count);

	std::size_t vertex_index = 0u;
	for (unsigned int major = 0u; major < major_vertex_count; ++major) {
		auto const phi = major == major_edge_count
		               ? 0.0f
		               : static_cast<float>(major) * d_phi;
		auto const sin_phi = std::sin(phi);
		auto const cos_phi = std::cos(phi);

		for (unsigned int minor = 0u; minor < minor_vertex_count; ++minor) {
			auto const theta = minor == minor_edge_count
			                 ? 0.0f
			                 : static_cast<float>(minor) * d_theta;
			auto const sin_theta = std::sin(theta);
			auto const cos_theta = std::cos(theta);
			auto const ring_radius = major_radius + minor_radius * cos_theta;

			vertices[vertex_index] = glm::vec3(
				ring_radius * cos_phi,
				-minor_radius * sin_theta,
				ring_radius * sin_phi);

			tangents[vertex_index] = glm::normalize(glm::vec3(
				-sin_theta * cos_phi,
				-cos_theta,
				-sin_theta * sin_phi));
			binormals[vertex_index] = glm::normalize(glm::vec3(
				-sin_phi, 0.0f, cos_phi));
			normals[vertex_index] = glm::normalize(glm::cross(
				binormals[vertex_index], tangents[vertex_index]));

			texcoords[vertex_index] = glm::vec3(
				static_cast<float>(major) / static_cast<float>(major_edge_count),
				static_cast<float>(minor) / static_cast<float>(minor_edge_count),
				0.0f);

			++vertex_index;
		}
	}

	auto index_sets = std::vector<glm::uvec3>(
		2u * major_edge_count * minor_edge_count);
	std::size_t index = 0u;
	for (unsigned int major = 0u; major < major_edge_count; ++major) {
		for (unsigned int minor = 0u; minor < minor_edge_count; ++minor) {
			auto const current = minor_vertex_count * major + minor;
			auto const next_major = current + minor_vertex_count;

			index_sets[index++] = glm::uvec3(
				current, next_major, next_major + 1u);
			index_sets[index++] = glm::uvec3(
				current, next_major + 1u, current + 1u);
		}
	}

	return uploadParametricSurface(
		vertices, normals, texcoords, tangents, binormals, index_sets);
}

bonobo::mesh_data
parametric_shapes::createCircleRing(float const radius,
                                    float const spread_length,
                                    unsigned int const circle_split_count,
                                    unsigned int const spread_split_count)
{
	auto const circle_slice_edges_count = circle_split_count + 1u;
	auto const spread_slice_edges_count = spread_split_count + 1u;
	auto const circle_slice_vertices_count = circle_slice_edges_count + 1u;
	auto const spread_slice_vertices_count = spread_slice_edges_count + 1u;
	auto const vertices_nb = circle_slice_vertices_count * spread_slice_vertices_count;

	auto vertices  = std::vector<glm::vec3>(vertices_nb);
	auto normals   = std::vector<glm::vec3>(vertices_nb);
	auto texcoords = std::vector<glm::vec3>(vertices_nb);
	auto tangents  = std::vector<glm::vec3>(vertices_nb);
	auto binormals = std::vector<glm::vec3>(vertices_nb);

	float const spread_start = radius - 0.5f * spread_length;
	float const d_theta = glm::two_pi<float>() / (static_cast<float>(circle_slice_edges_count));
	float const d_spread = spread_length / (static_cast<float>(spread_slice_edges_count));

	// generate vertices iteratively
	size_t index = 0u;
	float theta = 0.0f;
	for (unsigned int i = 0u; i < circle_slice_vertices_count; ++i) {
		float const cos_theta = std::cos(theta);
		float const sin_theta = std::sin(theta);

		float distance_to_centre = spread_start;
		for (unsigned int j = 0u; j < spread_slice_vertices_count; ++j) {
			// vertex
			vertices[index] = glm::vec3(distance_to_centre * cos_theta,
			                            distance_to_centre * sin_theta,
			                            0.0f);

			// texture coordinates
			texcoords[index] = glm::vec3(static_cast<float>(j) / (static_cast<float>(spread_slice_vertices_count)),
			                             static_cast<float>(i) / (static_cast<float>(circle_slice_vertices_count)),
			                             0.0f);

			// tangent
			auto const t = glm::vec3(cos_theta, sin_theta, 0.0f);
			tangents[index] = t;

			// binormal
			auto const b = glm::vec3(-sin_theta, cos_theta, 0.0f);
			binormals[index] = b;

			// normal
			auto const n = glm::cross(t, b);
			normals[index] = n;

			distance_to_centre += d_spread;
			++index;
		}

		theta += d_theta;
	}

	// create index array
	auto index_sets = std::vector<glm::uvec3>(2u * circle_slice_edges_count * spread_slice_edges_count);

	// generate indices iteratively
	index = 0u;
	for (unsigned int i = 0u; i < circle_slice_edges_count; ++i)
	{
		for (unsigned int j = 0u; j < spread_slice_edges_count; ++j)
		{
			index_sets[index] = glm::uvec3(spread_slice_vertices_count * (i + 0u) + (j + 0u),
			                               spread_slice_vertices_count * (i + 0u) + (j + 1u),
			                               spread_slice_vertices_count * (i + 1u) + (j + 1u));
			++index;

			index_sets[index] = glm::uvec3(spread_slice_vertices_count * (i + 0u) + (j + 0u),
			                               spread_slice_vertices_count * (i + 1u) + (j + 1u),
			                               spread_slice_vertices_count * (i + 1u) + (j + 0u));
			++index;
		}
	}

	return uploadParametricSurface(
		vertices, normals, texcoords, tangents, binormals, index_sets);

	//bonobo::mesh_data data;
	//glGenVertexArrays(1, &data.vao);
	//assert(data.vao != 0u);
	//glBindVertexArray(data.vao);

	//auto const vertices_offset = 0u;
	//auto const vertices_size = static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3));
	//auto const normals_offset = vertices_size;
	//auto const normals_size = static_cast<GLsizeiptr>(normals.size() * sizeof(glm::vec3));
	//auto const texcoords_offset = normals_offset + normals_size;
	//auto const texcoords_size = static_cast<GLsizeiptr>(texcoords.size() * sizeof(glm::vec3));
	//auto const tangents_offset = texcoords_offset + texcoords_size;
	//auto const tangents_size = static_cast<GLsizeiptr>(tangents.size() * sizeof(glm::vec3));
	//auto const binormals_offset = tangents_offset + tangents_size;
	//auto const binormals_size = static_cast<GLsizeiptr>(binormals.size() * sizeof(glm::vec3));
	//auto const bo_size = static_cast<GLsizeiptr>(vertices_size
	//                                            +normals_size
	//                                            +texcoords_size
	//                                            +tangents_size
	//                                            +binormals_size
	//                                            );
	//glGenBuffers(1, &data.bo);
	//assert(data.bo != 0u);
	//glBindBuffer(GL_ARRAY_BUFFER, data.bo);
	//glBufferData(GL_ARRAY_BUFFER, bo_size, nullptr, GL_STATIC_DRAW);

	//glBufferSubData(GL_ARRAY_BUFFER, vertices_offset, vertices_size, static_cast<GLvoid const*>(vertices.data()));
	//glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::vertices));
	//glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::vertices), 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid const*>(0x0));

	//glBufferSubData(GL_ARRAY_BUFFER, normals_offset, normals_size, static_cast<GLvoid const*>(normals.data()));
	//glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::normals));
	//glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::normals), 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid const*>(normals_offset));

	//glBufferSubData(GL_ARRAY_BUFFER, texcoords_offset, texcoords_size, static_cast<GLvoid const*>(texcoords.data()));
	//glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::texcoords));
	//glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::texcoords), 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid const*>(texcoords_offset));

	//glBufferSubData(GL_ARRAY_BUFFER, tangents_offset, tangents_size, static_cast<GLvoid const*>(tangents.data()));
	//glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::tangents));
	//glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::tangents), 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid const*>(tangents_offset));

	//glBufferSubData(GL_ARRAY_BUFFER, binormals_offset, binormals_size, static_cast<GLvoid const*>(binormals.data()));
	//glEnableVertexAttribArray(static_cast<unsigned int>(bonobo::shader_bindings::binormals));
	//glVertexAttribPointer(static_cast<unsigned int>(bonobo::shader_bindings::binormals), 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid const*>(binormals_offset));

	//glBindBuffer(GL_ARRAY_BUFFER, 0u);

	//data.indices_nb = static_cast<GLsizei>(index_sets.size() * 3u);
	//glGenBuffers(1, &data.ibo);
	//assert(data.ibo != 0u);
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo);
	//glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_sets.size() * sizeof(glm::uvec3)), reinterpret_cast<GLvoid const*>(index_sets.data()), GL_STATIC_DRAW);

	//glBindVertexArray(0u);
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);

	//return data;
}

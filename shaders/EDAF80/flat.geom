#version 410

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

uniform vec3 light_position;
uniform vec3 camera_position;
uniform float shininess_value;

in VS_OUT {
	vec3 world_position;
	vec2 texcoord;
} gs_in[];

out GS_OUT {
	flat float diffuse_factor;
	flat float specular_factor;
	vec2 texcoord;
} gs_out;

void main()
{
	vec3 edge_1 = gs_in[1].world_position - gs_in[0].world_position;
	vec3 edge_2 = gs_in[2].world_position - gs_in[0].world_position;
	vec3 face_normal = normalize(cross(edge_1, edge_2));
	vec3 face_centre = (gs_in[0].world_position
	                  + gs_in[1].world_position
	                  + gs_in[2].world_position) / 3.0;

	vec3 light_direction = normalize(light_position - face_centre);
	vec3 view_direction = normalize(camera_position - face_centre);
	float diffuse_factor = max(dot(face_normal, light_direction), 0.0);

	float specular_factor = 0.0;
	if (diffuse_factor > 0.0) {
		vec3 reflected_light = reflect(-light_direction, face_normal);
		specular_factor = pow(max(dot(reflected_light, view_direction), 0.0),
		                      shininess_value);
	}

	for (int vertex_index = 0; vertex_index < 3; ++vertex_index) {
		gs_out.diffuse_factor = diffuse_factor;
		gs_out.specular_factor = specular_factor;
		gs_out.texcoord = gs_in[vertex_index].texcoord;
		gl_Position = gl_in[vertex_index].gl_Position;
		EmitVertex();
	}
	EndPrimitive();
}

#version 410

layout (location = 0) in vec3 vertex;

uniform mat4 vertex_model_to_world;
uniform mat4 vertex_world_to_clip;

out VS_OUT {
	vec3 cubemap_direction;
} vs_out;

void main()
{
	vec4 world_position = vertex_model_to_world * vec4(vertex, 1.0);
	// Keep only rotation/scale for the cubemap lookup. The translation in
	// vertex_model_to_world merely keeps the sphere centred on the camera.
	vs_out.cubemap_direction = mat3(vertex_model_to_world) * vertex;

	vec4 clip_position = vertex_world_to_clip * world_position;
	// After the perspective divide, z = w / w = 1: the skybox is always at
	// the farthest representable depth.
	gl_Position = clip_position.xyww;
}

#version 410

uniform bool has_diffuse_texture;
uniform bool has_opacity_texture;
uniform sampler2D diffuse_texture;
uniform sampler2D opacity_texture;
uniform vec3 material_diffuse;
uniform vec3 light_direction;

in VS_OUT
{
	vec3 world_normal;
	vec2 texcoord;
} fs_in;

layout (location = 0) out vec4 frag_color;
layout (location = 1) out vec4 frag_normal;

void main()
{
	if (has_opacity_texture && texture(opacity_texture, fs_in.texcoord).r < 0.5)
		discard;

	vec3 albedo = has_diffuse_texture
		? texture(diffuse_texture, fs_in.texcoord).rgb
		: max(material_diffuse, vec3(0.05));
	vec3 world_normal = normalize(fs_in.world_normal);
	float diffuse_light = max(dot(world_normal,
	                              normalize(light_direction)), 0.0);
	vec3 lighting = vec3(0.25 + 0.75 * diffuse_light);
	frag_color = vec4(albedo * lighting, 1.0);
	frag_normal = vec4(world_normal * 0.5 + 0.5, 1.0);
}

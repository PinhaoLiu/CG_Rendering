#version 410

uniform samplerCube cubemap_texture;

in VS_OUT {
	vec3 cubemap_direction;
} fs_in;

out vec4 frag_color;

void main()
{
	frag_color = texture(cubemap_texture, normalize(fs_in.cubemap_direction));
}

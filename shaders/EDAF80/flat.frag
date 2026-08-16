#version 410

uniform vec3 ambient_colour;
uniform vec3 diffuse_colour;
uniform vec3 specular_colour;

uniform sampler2D diffuse_texture;
uniform sampler2D specular_texture;
uniform int has_diffuse_texture;
uniform int has_specular_texture;

in GS_OUT {
	flat float diffuse_factor;
	flat float specular_factor;
	vec2 texcoord;
} fs_in;

out vec4 frag_color;

void main()
{
	vec3 diffuse_reflectance = diffuse_colour;
	if (has_diffuse_texture != 0)
		diffuse_reflectance *= texture(diffuse_texture, fs_in.texcoord).rgb;

	vec3 specular_reflectance = specular_colour;
	if (has_specular_texture != 0)
		specular_reflectance *= texture(specular_texture, fs_in.texcoord).rgb;

	vec3 colour = ambient_colour
	            + diffuse_reflectance * fs_in.diffuse_factor
	            + specular_reflectance * fs_in.specular_factor;
	frag_color = vec4(colour, 1.0);
}

#version 410

layout (location = 0) in vec3 particle_position;

uniform mat4 uWorldToClip;
uniform float uPointSizePixels;

out float view_depth;

void main()
{
	vec4 clip_position = uWorldToClip * vec4(particle_position, 1.0);
	gl_Position = clip_position;
	gl_PointSize = uPointSizePixels;
	view_depth = clip_position.z / clip_position.w;
}

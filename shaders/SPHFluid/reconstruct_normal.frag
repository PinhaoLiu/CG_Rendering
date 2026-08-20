#version 430

in vec2 uv;

layout (location = 0) out vec3 reconstructed_normal;

layout (binding = 0) uniform sampler2D uLinearDepth;
uniform mat4 uClipToView;
uniform mat4 uViewToWorld;
uniform float uDepthSentinel;
uniform bool uOutputWorldSpace;

bool valid_depth(float depth)
{
	return !isnan(depth) && !isinf(depth) &&
		depth > 0.0 && depth < 0.5 * uDepthSentinel;
}

bool finite_vec3(vec3 value)
{
	return !any(isnan(value)) && !any(isinf(value));
}

vec3 view_position(ivec2 pixel, float depth, ivec2 dimensions)
{
	vec2 pixel_uv = (vec2(pixel) + vec2(0.5)) / vec2(dimensions);
	vec4 view_h = uClipToView * vec4(pixel_uv * 2.0 - 1.0, 1.0, 1.0);
	vec3 view_ray = view_h.xyz / view_h.w;
	// Linear depth stores positive -viewPosition.z, not radial distance.
	return view_ray * (depth / max(-view_ray.z, 1.0e-8));
}

bool neighbour_position(ivec2 pixel, ivec2 dimensions, out vec3 position,
	                    out float depth)
{
	if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, dimensions)))
		return false;
	depth = texelFetch(uLinearDepth, pixel, 0).r;
	if (!valid_depth(depth))
		return false;
	position = view_position(pixel, depth, dimensions);
	return finite_vec3(position);
}

vec3 stable_difference(vec3 negative_position, float negative_depth, bool has_negative,
	                   vec3 positive_position, float positive_depth, bool has_positive,
	                   vec3 centre, float centre_depth)
{
	if (!has_negative)
		return positive_position - centre;
	if (!has_positive)
		return centre - negative_position;
	float negative_delta = abs(negative_depth - centre_depth);
	float positive_delta = abs(positive_depth - centre_depth);
	float symmetric_tolerance = max(1.0e-5,
		0.1 * min(negative_delta, positive_delta));
	if (abs(negative_delta - positive_delta) <= symmetric_tolerance)
		return 0.5 * (positive_position - negative_position);
	return positive_delta < negative_delta
		? positive_position - centre : centre - negative_position;
}

void main()
{
	ivec2 dimensions = textureSize(uLinearDepth, 0);
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	float centre_depth = texelFetch(uLinearDepth, pixel, 0).r;
	if (!valid_depth(centre_depth)) {
		reconstructed_normal = vec3(0.0);
		return;
	}

	vec3 centre = view_position(pixel, centre_depth, dimensions);
	vec3 left_position = vec3(0.0);
	vec3 right_position = vec3(0.0);
	vec3 down_position = vec3(0.0);
	vec3 up_position = vec3(0.0);
	float left_depth = 0.0;
	float right_depth = 0.0;
	float down_depth = 0.0;
	float up_depth = 0.0;
	bool has_left = neighbour_position(pixel + ivec2(-1, 0), dimensions,
	                                  left_position, left_depth);
	bool has_right = neighbour_position(pixel + ivec2(1, 0), dimensions,
	                                   right_position, right_depth);
	bool has_down = neighbour_position(pixel + ivec2(0, -1), dimensions,
	                                  down_position, down_depth);
	bool has_up = neighbour_position(pixel + ivec2(0, 1), dimensions,
	                                up_position, up_depth);
	if ((!has_left && !has_right) || (!has_down && !has_up)) {
		reconstructed_normal = vec3(0.0);
		return;
	}

	// Prefer the neighbour on the same surface layer. Derivative signs remain
	// +x/+y regardless of whether the forward or backward difference wins.
	vec3 dPdx = stable_difference(left_position, left_depth, has_left,
	                              right_position, right_depth, has_right,
	                              centre, centre_depth);
	vec3 dPdy = stable_difference(down_position, down_depth, has_down,
	                              up_position, up_depth, has_up,
	                              centre, centre_depth);
	vec3 unnormalized = cross(dPdy, dPdx);
	float length_squared = dot(unnormalized, unnormalized);
	if (isnan(length_squared) || isinf(length_squared) || length_squared <= 1.0e-20) {
		reconstructed_normal = vec3(0.0);
		return;
	}

	vec3 normal = unnormalized * inversesqrt(length_squared);
	if (dot(normal, -centre) < 0.0)
		normal = -normal;
	if (uOutputWorldSpace)
		normal = normalize(mat3(uViewToWorld) * normal);
	reconstructed_normal = finite_vec3(normal) ? normal : vec3(0.0);
}

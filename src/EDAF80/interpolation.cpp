#include "interpolation.hpp"

namespace
{
	struct CatmullRomCoefficients
	{
		glm::vec3 c0;
		glm::vec3 c1;
		glm::vec3 c2;
		glm::vec3 c3;
	};

	CatmullRomCoefficients computeCatmullRomCoefficients(
		glm::vec3 const& p0, glm::vec3 const& p1,
		glm::vec3 const& p2, glm::vec3 const& p3,
		float const t)
	{
		return {
			p1,
			-t * p0 + t * p2,
			2.0f * t * p0 + (t - 3.0f) * p1
				+ (3.0f - 2.0f * t) * p2 - t * p3,
			-t * p0 + (2.0f - t) * p1
				+ (t - 2.0f) * p2 + t * p3
		};
	}
}

glm::vec3
interpolation::evalLERP(glm::vec3 const& p0, glm::vec3 const& p1, float const x)
{
	return (1.0f - x) * p0 + x * p1;
}

glm::vec3
interpolation::evalCatmullRom(glm::vec3 const& p0, glm::vec3 const& p1,
                              glm::vec3 const& p2, glm::vec3 const& p3,
                              float const t, float const x)
{
	auto const x2 = x * x;
	auto const x3 = x2 * x;
	auto const coefficients = computeCatmullRomCoefficients(p0, p1, p2, p3, t);

	return coefficients.c0 + x * coefficients.c1
	     + x2 * coefficients.c2 + x3 * coefficients.c3;
}

glm::vec3
interpolation::evalCatmullRomDerivative(
	glm::vec3 const& p0, glm::vec3 const& p1,
	glm::vec3 const& p2, glm::vec3 const& p3,
	float const t, float const x)
{
	auto const coefficients = computeCatmullRomCoefficients(p0, p1, p2, p3, t);
	return coefficients.c1 + 2.0f * x * coefficients.c2
	     + 3.0f * x * x * coefficients.c3;
}

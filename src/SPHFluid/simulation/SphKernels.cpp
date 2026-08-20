#include "simulation/SphKernels.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

namespace sph
{
	SphKernels::SphKernels(float const smoothing_radius) :
		mSmoothingRadius(smoothing_radius),
		mSquaredSmoothingRadius(smoothing_radius * smoothing_radius),
		mPoly6Coefficient(0.0f),
		mSpikyGradientCoefficient(0.0f),
		mViscosityLaplacianCoefficient(0.0f)
	{
		if (!(mSmoothingRadius > 0.0f) || !std::isfinite(mSmoothingRadius))
			throw std::invalid_argument("SPH smoothing radius must be finite and positive.");

		float const h2 = mSquaredSmoothingRadius;
		float const h3 = h2 * mSmoothingRadius;
		float const h6 = h3 * h3;
		float const h9 = h6 * h3;
		mPoly6Coefficient = 315.0f / (64.0f * glm::pi<float>() * h9);
		mSpikyGradientCoefficient = -45.0f / (glm::pi<float>() * h6);
		mViscosityLaplacianCoefficient = 45.0f / (glm::pi<float>() * h6);
	}

	float SphKernels::poly6(float const squared_distance) const noexcept
	{
		if (squared_distance < 0.0f || squared_distance >= mSquaredSmoothingRadius)
			return 0.0f;
		float const difference = mSquaredSmoothingRadius - squared_distance;
		return mPoly6Coefficient * difference * difference * difference;
	}

	glm::vec3 SphKernels::spikyGradient(glm::vec3 const& displacement,
	                                    float const distance) const noexcept
	{
		if (!(distance > 0.0f) || distance >= mSmoothingRadius)
			return glm::vec3(0.0f);
		float const difference = mSmoothingRadius - distance;
		return mSpikyGradientCoefficient * difference * difference *
		       (displacement / distance);
	}

	float SphKernels::viscosityLaplacian(float const distance) const noexcept
	{
		if (distance < 0.0f || distance >= mSmoothingRadius)
			return 0.0f;
		return mViscosityLaplacianCoefficient * (mSmoothingRadius - distance);
	}

	float SphKernels::smoothingRadius() const noexcept
	{
		return mSmoothingRadius;
	}

	float SphKernels::squaredSmoothingRadius() const noexcept
	{
		return mSquaredSmoothingRadius;
	}
}

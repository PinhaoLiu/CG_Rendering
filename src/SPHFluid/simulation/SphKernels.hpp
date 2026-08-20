#pragma once

#include <glm/vec3.hpp>

namespace sph
{
	class SphKernels
	{
	public:
		explicit SphKernels(float smoothing_radius);

		float poly6(float squared_distance) const noexcept;
		glm::vec3 spikyGradient(glm::vec3 const& displacement,
		                              float distance) const noexcept;
		float viscosityLaplacian(float distance) const noexcept;

		float smoothingRadius() const noexcept;
		float squaredSmoothingRadius() const noexcept;

	private:
		float mSmoothingRadius;
		float mSquaredSmoothingRadius;
		float mPoly6Coefficient;
		float mSpikyGradientCoefficient;
		float mViscosityLaplacianCoefficient;
	};
}

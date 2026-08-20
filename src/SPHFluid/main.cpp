#include "app/SphApplication.hpp"

#include "core/Bonobo.h"
#include "core/Log.h"

#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
	std::setlocale(LC_ALL, "");

	Bonobo framework;
	try {
		sph::SphApplication application(framework.GetWindowManager());
		return application.run();
	} catch (std::exception const& error) {
		LogError("SPH_Fluid failed: %s", error.what());
		std::cerr << "SPH_Fluid failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}

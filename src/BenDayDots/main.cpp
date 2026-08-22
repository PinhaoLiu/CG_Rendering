#include "app/BenDayApplication.hpp"

#include "core/Bonobo.h"
#include "core/Log.h"

#include <clocale>
#include <cstdlib>
#include <exception>

int main()
{
	std::setlocale(LC_ALL, "");

	Bonobo framework;
	try {
		benday::BenDayApplication application(framework.GetWindowManager());
		return application.run();
	} catch (std::exception const& error) {
		LogError("Ben-Day Dots failed: %s", error.what());
		return EXIT_FAILURE;
	}
}

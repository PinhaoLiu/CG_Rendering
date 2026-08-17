add_executable (EDAN35_Assignment1)

target_sources (
	EDAN35_Assignment1
	PRIVATE
		[[assignment1.hpp]]
		[[assignment1.cpp]]
)

target_link_libraries (EDAN35_Assignment1 PRIVATE assignment_setup)

install (TARGETS EDAN35_Assignment1 DESTINATION bin)

copy_dlls (EDAN35_Assignment1 "${CMAKE_CURRENT_BINARY_DIR}")


add_executable (EDAN35_Assignment2)

target_sources (
	EDAN35_Assignment2
	PRIVATE
		[[assignment2.hpp]]
		[[assignment2.cpp]]
)

target_link_libraries (EDAN35_Assignment2 PRIVATE assignment_setup)

install (TARGETS EDAN35_Assignment2 DESTINATION bin)

copy_dlls (EDAN35_Assignment2 "${CMAKE_CURRENT_BINARY_DIR}")

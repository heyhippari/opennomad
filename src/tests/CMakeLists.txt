set(NAME "TestRunner")

add_library(${NAME} OBJECT ${PROJECT_SOURCE_DIR}/src/tests/TestRunner.cpp)
target_link_libraries(${NAME} PRIVATE project_options project_warnings doctest::doctest)
opennomad_enable_quality(${NAME})

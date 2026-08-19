set(NAME "TestRunner")

add_library(${NAME} OBJECT ${PROJECT_SOURCE_DIR}/src/tests/TestRunner.cpp)
target_link_libraries(${NAME} PUBLIC doctest::doctest)

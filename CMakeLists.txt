cmake_minimum_required(VERSION 3.15)
project(server CXX)

find_package(Boost REQUIRED CONFIG)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::headers Boost::url)

install(TARGETS server DESTINATION "."
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

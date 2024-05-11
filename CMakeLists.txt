cmake_minimum_required (VERSION 3.16)
project (Owen 
	VERSION 1.0
	LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (glog 0.7.0 REQUIRED)

find_package (X11 REQUIRED)

add_executable (Owen 
	main.cpp
	owen.cpp)

target_link_libraries (Owen 
	glog::glog
	X11::X11)

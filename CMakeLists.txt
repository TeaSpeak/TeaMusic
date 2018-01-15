cmake_minimum_required(VERSION 3.6)
project(TeaMusic-Provider)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fpermissive -Wall -Wno-sign-compare -Wno-reorder -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/enviroment/)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(../shared/src)
add_definitions(-DLTM_DESC)

#the extra providers
add_library(TeaMusicProviderOpus SHARED providers/opus/OpusProvider.cpp providers/opus/OpusMusicPlayer.cpp)
add_library(TeaMusicProviderYT SHARED providers/yt/YTProvider.cpp providers/yt/YTVManager.cpp providers/yt/YoutubeMusicPlayer.cpp)

set_target_properties(TeaMusicProviderOpus TeaMusicProviderYT
		PROPERTIES
		CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/enviroment/providers"
		CMAKE_SHARED_LIBRARY_PREFIX ""
)
set_target_properties(TeaMusicProviderOpus
		PROPERTIES
		PREFIX "000"
)
set_target_properties(TeaMusicProviderYT
		PROPERTIES
		PREFIX "001"
)

target_link_libraries(TeaMusicProviderOpus TeaMusic opus opusfile)
target_link_libraries(TeaMusicProviderYT TeaMusic TeaMusicProviderOpus ThreadPoolStatic stdc++fs.a)

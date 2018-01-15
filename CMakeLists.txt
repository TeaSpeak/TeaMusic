cmake_minimum_required(VERSION 3.6)
project(TeaMusic-Provider)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fpermissive -Wall -Wno-sign-compare -Wno-reorder -static-libgcc -static-libstdc++")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/providers")

option(BUILD_PROVIDER_OPUS "Build the opus provider" ON)
if(BUILD_PROVIDER_OPUS)
	message("Building OPUS provider")
	add_library(ProviderOpus SHARED providers/opus/OpusProvider.cpp providers/opus/OpusMusicPlayer.cpp)
	target_link_libraries(ProviderOpus TeaMusic opus opusfile)
	set_target_properties(ProviderOpus
			PROPERTIES
			PREFIX "000" #Library load order (dont requre other providers)
	)
endif()

if(EXISTS ../shared/src/)
	include_directories(../shared/src)
	option(BUILD_PROVIDER_YT "Build the Youtube-dl provider. (You requre extra headers)" ON)
else()
	option(BUILD_PROVIDER_YT "Build the Youtube-dl provider. (You requre extra headers)" OFF)
endif()
if (BUILD_PROVIDER_YT)
	message("Building YouTube provider")
	add_library(ProviderYT SHARED providers/yt/YTProvider.cpp providers/yt/YTVManager.cpp providers/yt/YoutubeMusicPlayer.cpp)
	target_link_libraries(ProviderYT TeaMusic ProviderOpus ThreadPoolStatic stdc++fs.a)
	set_target_properties(ProviderYT
			PROPERTIES
			PREFIX "001" #Library load order (Requires opus provider to load)
	)
endif ()

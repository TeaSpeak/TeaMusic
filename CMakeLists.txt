cmake_minimum_required(VERSION 3.6)
project(TeaMusic-Provider)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fpermissive -Wall -Wno-sign-compare -static-libgcc -static-libstdc++")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/providers")

set(HEADERS include/teaspeak/MusicPlayer.h)

option(BUILD_PROVIDER_YT "Build the Youtube-dl provider. (It requires extra headers)" ON)
option(BUILD_PROVIDER_FFMPEG "Build the FFMpeg provider. (It requires extra headers)" ON)
option(BUILD_HELPERS "Build the development helper classes" ON)

if(NOT EXISTS ../shared/src/)
	set(LIBRARY_PATH_THREAD_POOL "ThreadPoolStatic")
	set(LIBRARY_PATH_JSON "jsoncpp_static")
	set(LIBRARY_PATH_VARIBALES "StringVariablesStatic")
endif()

include_directories(include)

if (BUILD_PROVIDER_YT)
	message("Building YouTube provider")
	add_library(ProviderYT SHARED
			providers/yt/YTProvider.cpp
			providers/yt/YTVManager.cpp
			providers/yt/YoutubeMusicPlayer.cpp
			providers/yt/YTRegex.cpp
			providers/shared/libevent.cpp
			providers/shared/CommandWrapper.cpp)
	target_link_libraries(ProviderYT ${StringVariable_LIBRARIES_STATIC} jsoncpp_lib threadpool::static ProviderFFMpeg)
	#The Youtube provider requires this libraries:
	#- TeaMusic
	#- ProviderOpus
	#- stdc++fs.a
	set_target_properties(ProviderYT
			PROPERTIES
			PREFIX "001" #Library load order (Requires opus provider to load)
	)
	target_compile_options(ProviderYT PUBLIC -fvisibility=hidden)
endif ()

if(BUILD_PROVIDER_FFMPEG)
	message("Building FFMpeg provider")
	add_library(ProviderFFMpeg SHARED
			providers/ffmpeg/FFMpegProvider.cpp
			providers/ffmpeg/FFMpegMusicPlayer.cpp 
			providers/ffmpeg/FFMpegMusicProcess.cpp
			providers/ffmpeg/FFMpegStream.cpp
			providers/shared/libevent.cpp)
	target_link_libraries(ProviderFFMpeg ${StringVariable_LIBRARIES_STATIC} threadpool::static)
	set_target_properties(ProviderFFMpeg
			PROPERTIES
			PREFIX "000" #Library load order (Requires nothink to load)
	)
	target_compile_options(ProviderFFMpeg PUBLIC -fvisibility=hidden)
endif()

if(BUILD_HELPERS AND OFF)
	message("Building helpers")
	add_executable(YoutubedlTest helpers/YoutubedlTest.cpp)
	target_link_libraries(YoutubedlTest ProviderFFMpeg ProviderYT)
	target_link_libraries(YoutubedlTest TeaMusic TeaSpeak dl stdc++fs CXXTerminal StringVariablesStatic libevent::core libevent::pthreads)
endif()
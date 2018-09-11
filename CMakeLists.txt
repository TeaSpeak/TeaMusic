cmake_minimum_required(VERSION 3.6)
project(TeaMusic-Provider)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fpermissive -Wall -Wno-sign-compare -static-libgcc -static-libstdc++ -fPIC")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/providers")

set(HEADERS include/MusicPlayer.h)

option(BUILD_PROVIDER_YT "Build the Youtube-dl provider. (You requre extra headers)" ON)
option(BUILD_PROVIDER_FFMPEG "Build the FFMpeg provider. (You requre extra headers)" ON)
option(BUILD_HELPERS "Build the development helper classes" ON)

if(NOT EXISTS ../shared/src/)
	set(LIBRARY_PATH_THREAD_POOL "ThreadPoolStatic")
	set(LIBRARY_PATH_JSON "jsoncpp_static")
	set(LIBRARY_PATH_VARIBALES "StringVariablesStatic")
endif()

if (BUILD_PROVIDER_YT)
	message("Building YouTube provider")
	add_library(ProviderYT SHARED ${HEADERS} providers/yt/YTProvider.cpp providers/yt/YTVManager.cpp providers/yt/YoutubeMusicPlayer.cpp)
	target_link_libraries(ProviderYT ${LIBRARY_PATH_VARIBALES} ${LIBRARY_PATH_JSON} ${LIBRARY_PATH_THREAD_POOL} ProviderFFMpeg)
	#The Youtube provider requires this libraries:
	#- TeaMusic
	#- ProviderOpus
	#- stdc++fs.a
	set_target_properties(ProviderYT
			PROPERTIES
			PREFIX "001" #Library load order (Requires opus provider to load)
	)
endif ()

if(BUILD_PROVIDER_FFMPEG)
	message("Building FFMpeg provider")
	add_library(ProviderFFMpeg SHARED ${HEADERS} providers/ffmpeg/FFMpegProvider.cpp providers/ffmpeg/FFMpegMusicPlayer.cpp providers/ffmpeg/FFMpegMusicProcess.cpp)
	target_link_libraries(ProviderFFMpeg ${LIBRARY_PATH_VARIBALES} ${LIBRARY_PATH_THREAD_POOL} ${LIBEVENT_PATH}/libevent.a)
	set_target_properties(ProviderFFMpeg
			PROPERTIES
			PREFIX "000" #Library load order (Requires nothink to load)
	)
endif()

if(BUILD_HELPERS)
	message("Building helpers")
	add_executable(YoutubedlTest helpers/YoutubedlTest.cpp)
	target_link_libraries(YoutubedlTest ProviderFFMpeg ProviderYT)
	target_link_libraries(YoutubedlTest TeaMusic TeaSpeak dl stdc++fs CXXTerminal StringVariablesStatic event_pthreads pthread)
endif()
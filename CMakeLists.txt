cmake_minimum_required(VERSION 3.6)
project(TeaMusic-Provider)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fpermissive -Wall -Wno-sign-compare -static-libgcc -static-libstdc++ -fPIC")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/providers")

set(HEADERS include/MusicPlayer.h)

if(EXISTS ../shared/src/)
	include_directories(../shared/src)
	SET(HAS_SHARED_SOURCE ON)
else()
	SET(HAS_SHARED_SOURCE OFF)
endif()

option(BUILD_PROVIDER_OPUS "Build the opus provider" ON)
option(BUILD_PROVIDER_YT "Build the Youtube-dl provider. (You requre extra headers)" ${HAS_SHARED_SOURCE})
option(BUILD_PROVIDER_FFMPEG "Build the FFMpeg provider. (You requre extra headers)" ${HAS_SHARED_SOURCE})
option(BUILD_HELPERS "Build the development helper classes" ON)

if(BUILD_PROVIDER_OPUS)
	message("Building OPUS provider")
	add_library(ProviderOpus SHARED ${HEADERS} providers/opus/OpusProvider.cpp providers/opus/OpusMusicPlayer.cpp)
	#The Youtube provider requires this libraries:
	#- TeaMusic
	#- opus
	#- opusfile
	#- ThreadPoolStatic
	target_link_libraries(ProviderOpus TeaMusic opus opusfile)
	set_target_properties(ProviderOpus
			PROPERTIES
			PREFIX "000" #Library load order (dont requre other providers)
	)
endif()

if (BUILD_PROVIDER_YT)
	message("Building YouTube provider")
	add_library(ProviderYT SHARED ${HEADERS} providers/yt/YTProvider.cpp providers/yt/YTVManager.cpp providers/yt/YoutubeMusicPlayer.cpp)
	target_link_libraries(ProviderYT /usr/local/lib/libjsoncpp_static.a ThreadPoolStatic ProviderFFMpeg)
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
	target_link_libraries(ProviderFFMpeg ThreadPoolStatic ${LIBEVENT_PATH}/libevent.a)
	#The FFMpeg provider requires this libraries:
	#- TeaMusic
	#- ThreadPoolStatic
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
#include "YoutubeMusicPlayer.h"
#include "YTVManager.h"
using namespace music::player;

YoutubeMusicPlayer::YoutubeMusicPlayer(std::shared_ptr<yt::AudioInfo> info) : FFMpegMusicPlayer(info->stream_url, info->live_stream), video(info) {}
YoutubeMusicPlayer::~YoutubeMusicPlayer() {}

std::string music::player::YoutubeMusicPlayer::songTitle() {
    return this->video->title;
}

std::string music::player::YoutubeMusicPlayer::songDescription() {
    return this->video->description;
}

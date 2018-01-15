#pragma once

#include "providers/opus/OpusMusicPlayer.h"

namespace yt {
    class AudioInfo;
}
namespace music {
    namespace player {
        class YoutubeMusicPlayer : public OpusMusicPlayer {
            public:
                YoutubeMusicPlayer(std::shared_ptr<yt::AudioInfo>);
                ~YoutubeMusicPlayer();

                std::string songTitle() override;

                std::string songDescription() override;

            private:
                std::shared_ptr<yt::AudioInfo> video;
        };
    }
}
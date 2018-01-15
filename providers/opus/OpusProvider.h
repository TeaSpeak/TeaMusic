#pragma once

#include <include/MusicPlayer.h>

extern "C" {
    std::shared_ptr<music::manager::PlayerProvider> EXPORT create_provider();
}
#include "player.h"
#include "prologic2_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <SDL.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <mutex>

static std::vector<float> g_audioQueue;
static std::mutex g_audioMutex;

static void sdlAudioCallback(void*, Uint8* stream, int len) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    size_t framesNeeded = len / (6 * sizeof(float));
    size_t framesAvail = g_audioQueue.size() / 6;
    size_t toCopy = std::min(framesNeeded, framesAvail);

    float* out = reinterpret_cast<float*>(stream);
    if (toCopy > 0) {
        std::memcpy(out, g_audioQueue.data(), toCopy * 6 * sizeof(float));
        g_audioQueue.erase(g_audioQueue.begin(),
                           g_audioQueue.begin() + toCopy * 6);
    }
    if (toCopy < framesNeeded) {
        std::memset(out + toCopy * 6, 0,
                    (framesNeeded - toCopy) * 6 * sizeof(float));
    }
}

Player::Player() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

Player::~Player() {
    close();
    SDL_Quit();
}

bool Player::open(const std::string& filepath) {
    if (avformat_open_input(&fmtCtx_, filepath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Cannot open file: " << filepath << std::endl;
        return false;
    }
    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) return false;

    for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
        auto* par = fmtCtx_->streams[i]->codecpar;
        if (videoStreamIdx_ < 0 && par->codec_type == AVMEDIA_TYPE_VIDEO)
            videoStreamIdx_ = i;
        if (audioStreamIdx_ < 0 && par->codec_type == AVMEDIA_TYPE_AUDIO)
            audioStreamIdx_ = i;
    }

    if (videoStreamIdx_ >= 0) {
        auto* par = fmtCtx_->streams[videoStreamIdx_]->codecpar;
        auto* codec = avcodec_find_decoder(par->codec_id);
        videoCtx_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(videoCtx_, par);
        avcodec_open2(videoCtx_, codec, nullptr);
    }

    if (audioStreamIdx_ >= 0) {
        auto* par = fmtCtx_->streams[audioStreamIdx_]->codecpar;
        auto* codec = avcodec_find_decoder(par->codec_id);
        audioCtx_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(audioCtx_, par);
        avcodec_open2(audioCtx_, codec, nullptr);

        double sr = audioCtx_->sample_rate;
        pl

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
        pl2_ = std::make_unique<ProLogic2Decoder>(sr);

        SDL_AudioSpec desired{};
        desired.freq = sr;
        desired.format = AUDIO_F32SYS;
        desired.channels = 6;
        desired.samples = 1024;
        desired.callback = sdlAudioCallback;

        if (SDL_OpenAudio(&desired, nullptr) < 0) {
            std::cerr << "SDL_OpenAudio failed: " << SDL_GetError() << std::endl;
            return false;
        }
        SDL_PauseAudio(0);
    }

    int w = videoCtx_ ? videoCtx_->width : 1280;
    int h = videoCtx_ ? videoCtx_->height : 720;
    window_ = SDL_CreateWindow("Surround Player - Pro Logic II",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w, h, SDL_WINDOW_RESIZABLE);
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (videoCtx_) {
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_IYUV,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
    }

    workFL_.resize(8192); workFR_.resize(8192); workC_.resize(8192);
    workLFE_.resize(8192); workRL_.resize(8192); workRR_.resize(8192);
    resampleBufL_.resize(8192); resampleBufR_.resize(8192);

    return true;
}

void Player::processAudioFrame(AVFrame* frame) {
    if (!pl2_) return;
    pl2_->setEnabled(surroundEnabled_);

    int nbSamples = frame->nb_samples;
    int numChannels = frame->ch_layout.nb_channels;

    if (numChannels >= 6 && frame->format == AV_SAMPLE_FMT_FLTP) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        size_t oldSize = g_audioQueue.size();
        g_audioQueue.resize(oldSize + nbSamples * 6);
        for (int i = 0; i < nbSamples; ++i) {
            for (int ch = 0; ch < 6; ++ch) {
                float* plane = reinterpret_cast<float*>(frame->data[ch]);
                g_audioQueue[oldSize + i * 6 + ch] = plane ? plane[i] : 0.0f;
            }
        }
        return;
    }

    const float* inL = nullptr;
    const float* inR = nullptr;

    if (frame->format == AV_SAMPLE_FMT_FLTP && numChannels >= 2) {
        inL = reinterpret_cast<const float*>(frame->data[0]);
        inR = reinterpret_cast<const float*>(frame->data[1]);
    } else if (frame->format == AV_SAMPLE_FMT_FLT && numChannels >= 2) {
        const float* interleaved = reinterpret_cast<const float*>(frame->data[0]);
        for (int i = 0; i < nbSamples; ++i) {
            resampleBufL_[i] = interleaved[i * numChannels + 0];
            resampleBufR_[i] = interleaved[i * numChannels + 1];
        }
        inL = resampleBufL_.data();
        inR = resampleBufR_.data();
    } else {
        if (!swrCtx_) {
            swrCtx_ = swr_alloc();
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_opt_set_chlayout(swrCtx_, "in_chlayout",  &stereo, 0);
            av_opt_set_chlayout(swrCtx_, "out_chlayout", &stereo, 0);
            av_opt_set_int(swrCtx_, "in_sample_rate",  audioCtx_->sample_rate, 0);
            av_opt_set_int(swrCtx_, "out_sample_rate", audioCtx_->sample_rate, 0);
            av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt",
                                  static_cast<AVSampleFormat>(frame->format), 0);
            av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
            swr_init(swrCtx_);
        }
        uint8_t* outBuf[2] = {
            reinterpret_cast<uint8_t*>(resampleBufL_.data()),
            reinterpret_cast<uint8_t*>(resampleBufR_.data())
        };
        int converted = swr_convert(swrCtx_, outBuf, nbSamples,
                                    (const uint8_t**)frame->data, nbSamples);
        inL = resampleBufL_.data();
        inR = resampleBufR_.data();
        nbSamples = converted;
    }

    if (!inL || !inR) return;

    if (workFL_.size() < (size_t)nbSamples) {
        size_t ns = nbSamples * 2;
        workFL_.resize(ns); workFR_.resize(ns); workC_.resize(ns);
        workLFE_.resize(ns); workRL_.resize(ns); workRR_.resize(ns);
    }

    pl2_->process(inL, inR,
                  workFL_.data(), workFR_.data(), workC_.data(),
                  workLFE_.data(), workRL_.data(), workRR_.data(),
                  nbSamples);

    std::lock_guard<std::mutex> lock(g_audioMutex);
    size_t oldSize = g_audioQueue.size();
    g_audioQueue.resize(oldSize + nbSamples * 6);
    for (int i = 0; i < nbSamples; ++i) {
        g_audioQueue[oldSize + i * 6 + 0] = workFL_[i];
        g_audioQueue[oldSize + i * 6 + 1] = workFR_[i];
        g_audioQueue[oldSize + i * 6 + 2] = workC_[i];
        g_audioQueue[oldSize + i * 6 + 3] = workLFE_[i];
        g_audioQueue[oldSize + i * 6 + 4] = workRL_[i];
        g_audioQueue[oldSize + i * 6 + 5] = workRR_[i];
    }
}

void Player::renderVideoFrame(AVFrame* frame) {
    if (!texture_ || !renderer_) return;

    static struct SwsContext* swsCtx = nullptr;
    AVFrame* yuvFrame = av_frame_alloc();
    int w = frame->width, h = frame->height;
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
    av_image_fill_arrays(yuvFrame->data, yuvFrame->linesize, buffer,
                         AV_PIX_FMT_YUV420P, w, h, 1);

    swsCtx = sws_getCachedContext(swsCtx, w, h,
                                  (AVPixelFormat)frame->format,
                                  w, h, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    sws_scale(swsCtx, frame->data, frame->linesize, 0, h,
              yuvFrame->data, yuvFrame->linesize);

    SDL_UpdateYUVTexture(texture_, nullptr,
                         yuvFrame->data[0], yuvFrame->linesize[0],
                         yuvFrame->data[1], yuvFrame->linesize[1],
                         yuvFrame->data[2], yuvFrame->linesize[2]);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);

    av_free(buffer);
    av_frame_free(&yuvFrame);
}

void Player::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    bool running = true;
    Uint64 videoStart = 0;
    double videoTimeBase = 0.0;
    if (videoStreamIdx_ >= 0) {
        videoTimeBase = av_q2d(fmtCtx_->streams[videoStreamIdx_]->time_base);
        videoStart = SDL_GetPerformanceCounter();
    }

    while (running && av_read_frame(fmtCtx_, pkt) >= 0) {
        if (pkt->stream_index == videoStreamIdx_) {
            avcodec_send_packet(videoCtx_, pkt);
            while (avcodec_receive_frame(videoCtx_, frame) == 0) {
                double pts = frame->pts * videoTimeBase;
                Uint64 now = SDL_GetPerformanceCounter();
                double elapsed = (now - videoStart) / (double)SDL_GetPerformanceFrequency();
                double delay = pts - elapsed;
                if (delay > 0 && delay < 1.0) {
                    SDL_Delay((Uint32)(delay * 1000));
                }
                renderVideoFrame(frame);
            }
        } else if (pkt->stream_index == audioStreamIdx_) {
            avcodec_send_packet(audioCtx_, pkt);
            while (avcodec_receive_frame(audioCtx_, frame) == 0) {
                processAudioFrame(frame);
            }
        }
        av_packet_unref(pkt);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_s) {
                    toggleSurround();
                    std::cout << "Surround: "
                              << (surroundEnabled_ ? "ON" : "OFF") << std::endl;
                }
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void Player::play() {
    if (fmtCtx_) decodeLoop();
}

void Player::close() {
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    if (swrCtx_)   { swr_free(&swrCtx_); swrCtx_ = nullptr; }
    if (videoCtx_) { avcodec_free_context(&videoCtx_); }
    if (audioCtx_) { avcodec_free_context(&audioCtx_); }
    if (fmtCtx_)   { avformat_close_input(&fmtCtx_); }
    if (texture_)  { SDL_DestroyTexture(texture_); texture_ = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_); window_ = nullptr; }
}

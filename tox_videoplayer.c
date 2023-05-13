/**
 *
 * tox_videoplayer
 * (C)Zoff <zoff@zoff.cc> in 2023
 *
 * https://github.com/zoff99/__________
 *
 *
 */
/*
 * Copyright © 2023 Zoff <zoff@zoff.cc>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

/*
 * 
 gcc -O3 -g -fsanitize=address -static-libasan -fPIC tox_videoplayer.c $(pkg-config --cflags --libs libsodium libswresample opus vpx libavcodec libswscale libavformat libavutil x264) -pthread -o tox_videoplayer
 * 
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>

#include <sodium/utils.h>

// define this before including toxcore amalgamation -------
#define MIN_LOGGER_LEVEL LOGGER_LEVEL_DEBUG
// define this before including toxcore amalgamation -------

// include toxcore amalgamation with ToxAV --------
#include "toxcore_amalgamation.c"
// include toxcore amalgamation with ToxAV --------


#define DEFAULT_GLOBAL_AUD_BITRATE 32 // kbit/sec
#define DEFAULT_GLOBAL_VID_BITRATE 8000 // kbit/sec

static int self_online = 0;
static int friend_online = 0;
static pthread_t ffmpeg_thread;
static int ffmpeg_thread_stop = 1;
static const char *savedata_filename = "./savedata.tox";
static const char *savedata_tmp_filename = "./savedata.tox.tmp";
static char *global_decoder_string = "";
static char *global_encoder_string = "";

struct Node1 {
    char *ip;
    char *key;
    uint16_t udp_port;
    uint16_t tcp_port;
} nodes1[] = {
{ "2604:a880:1:20::32f:1001", "BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F", 33445, 33445 },
{ "tox.kurnevsky.net", "82EF82BA33445A1F91A7DB27189ECFC0C013E06E3DA71F588ED692BED625EC23", 33445, 33445 },
{ "tox1.mf-net.eu","B3E5FA80DC8EBD1149AD2AB35ED8B85BD546DEDE261CA593234C619249419506",33445,33445},
{ "tox3.plastiras.org","4B031C96673B6FF123269FF18F2847E1909A8A04642BBECD0189AC8AEEADAF64",33445,3389},
    { NULL, NULL, 0, 0 }
};

static void hex_string_to_bin2(const char *hex_string, uint8_t *output)
{
    size_t len = strlen(hex_string) / 2;
    size_t i = len;
    if (!output)
    {
        return;
    }
    const char *pos = hex_string;
    for (i = 0; i < len; ++i, pos += 2)
    {
        sscanf(pos, "%2hhx", &output[i]);
    }
}

static void bin2upHex(const uint8_t *bin, uint32_t bin_size, char *hex, uint32_t hex_size)
{
    sodium_bin2hex(hex, hex_size, bin, bin_size);

    for (size_t i = 0; i < hex_size - 1; i++) {
        hex[i] = toupper(hex[i]);
    }
}

// gives a counter value that increases every millisecond
static uint64_t current_time_monotonic_default2(void)
{
    struct timespec clock_mono;
    clock_gettime(CLOCK_MONOTONIC, &clock_mono);
    uint64_t time = 1000ULL * clock_mono.tv_sec + (clock_mono.tv_nsec / 1000000ULL);
    return time;
}

static void yieldcpu(uint32_t ms)
{
    usleep(1000 * ms);
}

static time_t get_unix_time(void)
{
    return time(NULL);
}

static void tox_log_cb__custom(Tox *tox, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func,
                        const char *message, void *user_data)
{
    printf("C-TOXCORE:1:%d:%s:%d:%s:%s\n", (int)level, file, (int)line, func, message);
}

static void updateToxSavedata(const Tox *tox)
{
    size_t size = tox_get_savedata_size(tox);
    uint8_t *savedata = calloc(1, size);
    tox_get_savedata(tox, savedata);

    FILE *f = fopen(savedata_tmp_filename, "wb");
    fwrite(savedata, size, 1, f);
    fclose(f);

    rename(savedata_tmp_filename, savedata_filename);
    free(savedata);
}

static void self_connection_change_callback(Tox *tox, TOX_CONNECTION status, void *userdata)
{
    switch (status) {
        case TOX_CONNECTION_NONE:
            printf("Lost connection to the Tox network.\n");
            self_online = 0;
            break;
        case TOX_CONNECTION_TCP:
            printf("Connected using TCP.\n");
            self_online = 1;
            break;
        case TOX_CONNECTION_UDP:
            printf("Connected using UDP.\n");
            self_online = 2;
            break;
    }
}

static void friendlist_onConnectionChange(Tox *tox, uint32_t friend_number, TOX_CONNECTION status, void *user_data)
{
    switch (status) {
        case TOX_CONNECTION_NONE:
            printf("Lost connection to friend.\n");
            friend_online = 0;
            break;
        case TOX_CONNECTION_TCP:
            printf("Connected to friend using TCP.\n");
            friend_online = 1;
            break;
        case TOX_CONNECTION_UDP:
            printf("Connected to friend using UDP.\n");
            friend_online = 2;
            break;
    }
}

static void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data)
{
    tox_friend_add_norequest(tox, public_key, NULL);
    updateToxSavedata(tox);
}

static void call_state_callback(ToxAV *av, uint32_t friend_number, uint32_t state, void *user_data)
{
    printf("ToxAV Call State change:fn=%d state=%d\n", friend_number, state);
}

static void call_callback(ToxAV *av, uint32_t friend_number, bool audio_enabled, bool video_enabled, void *user_data)
{
    printf("incoming ToxAV Call from fn=%d\n", friend_number);
    toxav_answer(av, friend_number, DEFAULT_GLOBAL_AUD_BITRATE, DEFAULT_GLOBAL_VID_BITRATE, NULL);
}

#ifdef TOX_HAVE_TOXAV_CALLBACKS_002
static void call_comm_callback(ToxAV *av, uint32_t friend_number, TOXAV_CALL_COMM_INFO comm_value,
                                 int64_t comm_number, void *user_data)
{
    if (comm_value == TOXAV_CALL_COMM_DECODER_IN_USE_VP8)
    {
        global_decoder_string = " VP8 ";
    }
    else if (comm_value == TOXAV_CALL_COMM_DECODER_IN_USE_H264)
    {
        global_decoder_string = " H264";
    }
    else if (comm_value == TOXAV_CALL_COMM_ENCODER_IN_USE_VP8)
    {
        global_encoder_string = " VP8 ";
        printf("ToxAV COMM: %s\n", global_encoder_string);
    }
    else if (comm_value == TOXAV_CALL_COMM_ENCODER_IN_USE_H264)
    {
        global_encoder_string = " H264";
        printf("ToxAV COMM: %s\n", global_encoder_string);
    }
}
#endif

static void *ffmpeg_thread_func(__attribute__((unused)) void *data)
{
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    AVCodec *audio_codec = NULL;
    AVCodec *video_codec = NULL;
    AVPacket packet;
    AVFrame *frame = NULL;
    int audio_stream_index = -1;
    int video_stream_index = -1;
    int num_samples;
    uint8_t **converted_samples = NULL;
    int ret;

    char *inputfile = (char *) data;

    // Open the input file
    if ((ret = avformat_open_input(&format_ctx, inputfile, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", inputfile);
        return NULL; // ret;
    }

    // Retrieve stream information
    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return NULL; // ret;
    }

    // Find the audio and video streams
    for (int i = 0; i < format_ctx->nb_streams; i++) {
        AVCodecParameters *codec_params = format_ctx->streams[i]->codecpar;
        AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
        if (!codec) {
            fprintf(stderr, "Unsupported codec!\n");
            continue;
        }
        if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index < 0) {
            audio_codec_ctx = avcodec_alloc_context3(codec);
            if (!audio_codec_ctx) {
                fprintf(stderr, "Could not allocate audio codec context\n");
                return NULL; // AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(audio_codec_ctx, codec_params)) < 0) {
                fprintf(stderr, "Could not copy audio codec parameters to context\n");
                return NULL; // ret;
            }
            if ((ret = avcodec_open2(audio_codec_ctx, codec, NULL)) < 0) {
                fprintf(stderr, "Could not open audio codec\n");
                return NULL; // ret;
            }
            audio_stream_index = i;
            audio_codec = codec;
        } else if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0) {
            video_codec_ctx = avcodec_alloc_context3(codec);
            if (!video_codec_ctx) {
                fprintf(stderr, "Could not allocate video codec context\n");
                return NULL; // AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(video_codec_ctx, codec_params)) < 0) {
                fprintf(stderr, "Could not copy video codec parameters to context\n");
                return NULL; // ret;
            }
            if ((ret = avcodec_open2(video_codec_ctx, codec, NULL)) < 0) {
                fprintf(stderr, "Could not open video codec\n");
                return NULL; // ret;
            }
            video_stream_index = i;
            video_codec = codec;
        }
    }

    // Make sure we found both audio and video streams
    if (audio_stream_index < 0 || video_stream_index < 0) {
        fprintf(stderr, "Could not find audio and video streams\n");
        return NULL; // AVERROR_EXIT;
    }

    // Allocate a frame for decoding
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        return NULL; // AVERROR(ENOMEM);
    }



    swr_ctx = swr_alloc_set_opts(NULL,
                                 AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, audio_codec_ctx->sample_rate,
                                 audio_codec_ctx->channel_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
                                 0, NULL);
    if (!swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        return NULL; // 1;
    }

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not initialize resampler context\n");
        return NULL; // 1;
    }




    // Allocate a buffer for the YUV data
    uint8_t *yuv_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
            video_codec_ctx->width, video_codec_ctx->height, 1));
    if (yuv_buffer == NULL) {
        fprintf(stderr, "Error: could not allocate YUV buffer\n");
        return NULL; // 1;
    }

    uint8_t *dst_yuv_buffer[3];
    dst_yuv_buffer[0] = yuv_buffer;
    dst_yuv_buffer[1] = yuv_buffer + (video_codec_ctx->width * video_codec_ctx->height);
    dst_yuv_buffer[2] = dst_yuv_buffer[1] + ((video_codec_ctx->width * video_codec_ctx->height) / 4);

    // Create a scaler context to convert the video to YUV
    struct SwsContext *scaler_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height,
            video_codec_ctx->pix_fmt, video_codec_ctx->width, video_codec_ctx->height,
            AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    if (scaler_ctx == NULL) {
        fprintf(stderr, "Error: could not create scaler context\n");
        return NULL; // 1;
    }

    fprintf(stderr, "SwsContext: %dx%d\n", video_codec_ctx->width, video_codec_ctx->height);


    while (friend_online == 0)
    {
        yieldcpu(100);
    }

    // Read packets from the input file and decode them
    while ((av_read_frame(format_ctx, &packet) >= 0) && (ffmpeg_thread_stop != 1))
    {
        if (packet.stream_index == audio_stream_index)
        {
            // Decode audio packet
            ret = avcodec_send_packet(audio_codec_ctx, &packet);
            if (ret < 0)
            {
                fprintf(stderr, "Error sending audio packet for decoding\n");
                break;
            }

            while (ret >= 0)
            {
               ret = avcodec_receive_frame(audio_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    fprintf(stderr, "Error during audio decoding\n");
                    break;
                }

                num_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                av_samples_alloc_array_and_samples(&converted_samples, NULL, 2, num_samples, AV_SAMPLE_FMT_S16, 0);
                swr_convert(swr_ctx, converted_samples, num_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);

                // Do something with the converted samples here
                int64_t pts = frame->pts;
                // fprintf(stderr, "AudioFrame:fn:%10d pts:%10ld sr:%5d ch:%1d samples:%3d\n",
                //     audio_codec_ctx->frame_number, pts, frame->sample_rate, 2, num_samples);

                av_freep(&converted_samples[0]);
                av_freep(&converted_samples);
            }
        }
        else if (packet.stream_index == video_stream_index)
        {
            // Decode video packet
            ret = avcodec_send_packet(video_codec_ctx, &packet);
            if (ret < 0)
            {
                fprintf(stderr, "Error sending video packet for decoding\n");
                break;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(video_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                } else if (ret < 0)
                {
                    fprintf(stderr, "Error during video decoding\n");
                    break;
                }

                // Convert the video frame to YUV
                int planes_stride[3];
                planes_stride[0] = av_image_get_linesize(AV_PIX_FMT_YUV420P, video_codec_ctx->width, 0);
                planes_stride[1] = av_image_get_linesize(AV_PIX_FMT_YUV420P, video_codec_ctx->width, 1);
                planes_stride[2] = av_image_get_linesize(AV_PIX_FMT_YUV420P, video_codec_ctx->width, 2);
                // fprintf(stderr, "VideoFrame:strides:%d %d %d\n",planes_stride[0],planes_stride[1],planes_stride[2]);

                sws_scale(scaler_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0, video_codec_ctx->height,
                        dst_yuv_buffer, planes_stride);

                int64_t pts = frame->pts;
                // fprintf(stderr, "VideoFrame:ls:%d %dx%d fn:%10d pts:%10ld pn:%10d\n",
                //             frame->linesize[0], frame->width, frame->height,
                //             video_codec_ctx->frame_number, pts, frame->coded_picture_number);

                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
        yieldcpu(1000 / 30);
    }

    // Clean up
    ffmpeg_thread = 1;
    pthread_join(ffmpeg_thread, NULL);
    //
    av_frame_free(&frame);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    av_free(yuv_buffer);
    swr_free(&swr_ctx);
    sws_freeContext(scaler_ctx);

    printf("ffmpeg Thread:Clean thread exit!\n");
    return NULL;
}



int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "no input file specified\n");
        return 0;
    }

    fprintf(stderr, "input file: %s\n", argv[1]);

    av_register_all();



    struct Tox_Options options;
    tox_options_default(&options);
    // ----- set options ------
    options.ipv6_enabled = true;
    options.local_discovery_enabled = true;
    options.hole_punching_enabled = true;
    options.udp_enabled = true;
    options.tcp_port = 0; // disable tcp relay function!
    options.log_callback = tox_log_cb__custom;
    // ----- set options ------

    FILE *f = fopen(savedata_filename, "rb");
    uint8_t *savedata = NULL;

    if (f)
    {
        fseek(f, 0, SEEK_END);
        size_t savedataSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        savedata = malloc(savedataSize);
        size_t ret = fread(savedata, savedataSize, 1, f);

        // TODO: handle ret return vlaue here!
        if (ret)
        {
            // ------
        }

        fclose(f);

        options.savedata_type = TOX_SAVEDATA_TYPE_TOX_SAVE;
        options.savedata_data = savedata;
        options.savedata_length = savedataSize;
    }


    self_online = 0;
    friend_online = 0;

#ifndef TOX_HAVE_TOXUTIL
    printf("init Tox\n");
    Tox *tox = tox_new(&options, NULL);
#else
    printf("init Tox [TOXUTIL]\n");
    Tox *tox = tox_utils_new(&options, NULL);
#endif
    printf("init ToxAV\n");
    ToxAV *toxav = toxav_new(tox, NULL);

    updateToxSavedata(tox);

    // ----- CALLBACKS -----
#ifdef TOX_HAVE_TOXUTIL
    tox_utils_callback_self_connection_status(tox, self_connection_change_callback);
    tox_callback_self_connection_status(tox, tox_utils_self_connection_status_cb);
    tox_utils_callback_friend_connection_status(tox, friendlist_onConnectionChange);
    tox_callback_friend_connection_status(tox, tox_utils_friend_connection_status_cb);
#else
    tox_callback_self_connection_status(tox, self_connection_change_callback);
#endif
    tox_callback_friend_request(tox, friend_request_cb);

    toxav_callback_call_state(toxav, call_state_callback, NULL);
    toxav_callback_call(toxav, call_callback, NULL);
#ifdef TOX_HAVE_TOXAV_CALLBACKS_002
    printf("have toxav_callback_call_comm\n");
    toxav_callback_call_comm(toxav, call_comm_callback, NULL);
#endif


    // ----- CALLBACKS -----
    // ----- bootstrap -----
    printf("Tox bootstrapping\n");
    for (int i = 0; nodes1[i].ip; i++)
    {
        uint8_t *key = (uint8_t *)calloc(1, 100);
        hex_string_to_bin2(nodes1[i].key, key);
        if (!key)
        {
            continue;
        }
        tox_bootstrap(tox, nodes1[i].ip, nodes1[i].udp_port, key, NULL);
        if (nodes1[i].tcp_port != 0)
        {
            tox_add_tcp_relay(tox, nodes1[i].ip, nodes1[i].tcp_port, key, NULL);
        }
        free(key);
    }
    // ----- bootstrap -----

    uint8_t tox_id_bin[tox_address_size()];
    tox_self_get_address(tox, tox_id_bin);
    int tox_address_hex_size = tox_address_size() * 2 + 1;
    char tox_id_hex[tox_address_hex_size];
    bin2upHex(tox_id_bin, tox_address_size(), tox_id_hex, tox_address_hex_size);
    printf("--------------------\n");
    printf("--------------------\n");
    printf("ToxID: %s\n", tox_id_hex);
    printf("--------------------\n");
    printf("--------------------\n");

    tox_iterate(tox, NULL);
    toxav_iterate(toxav);
    // ----------- wait for Tox to come online -----------
    while (1 == 1)
    {
        tox_iterate(tox, NULL);
        yieldcpu(tox_iteration_interval(tox));
        if (self_online > 0)
        {
            break;
        }
    }
    printf("Tox online\n");
    // ----------- wait for Tox to come online -----------



    ffmpeg_thread_stop = 0;
    if (pthread_create(&ffmpeg_thread, NULL, ffmpeg_thread_func, (void *)argv[1]) != 0)
    {
        printf("ffmpeg Thread create failed\n");
    }
    else
    {
        pthread_setname_np(ffmpeg_thread, "t_notif");
        printf("ffmpeg Thread successfully created\n");
    }


    // ----------- main loop -----------
    while (1 == 1)
    {
        tox_iterate(tox, NULL);
        yieldcpu(2);
        toxav_iterate(toxav);
        yieldcpu(2);
    }
    // ----------- main loop -----------







    // Clean up
    ffmpeg_thread = 1;
    pthread_join(ffmpeg_thread, NULL);

    toxav_kill(toxav);
    printf("killed ToxAV\n");
#ifndef TOX_HAVE_TOXUTIL
    tox_kill(tox);
    printf("killed Tox\n");
#else
    tox_utils_kill(tox);
    printf("killed Tox [TOXUTIL]\n");
#endif
    printf("--END--\n");

    return 0;
}

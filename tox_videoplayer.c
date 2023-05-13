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

#include <stdbool.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>

#include <sodium/utils.h>

// define this before including toxcore amalgamation -------
#define MIN_LOGGER_LEVEL LOGGER_LEVEL_WARNING
#define HW_CODEC_CONFIG_UTOX_UB81
#define HW_CODEC_CONFIG_HIGHVBITRATE
// define this before including toxcore amalgamation -------

// include toxcore amalgamation with ToxAV --------
#include "toxcore_amalgamation.c"
// include toxcore amalgamation with ToxAV --------


#define DEFAULT_GLOBAL_AUD_BITRATE 32 // kbit/sec
#define DEFAULT_GLOBAL_VID_BITRATE 8000 // kbit/sec

static int self_online = 0;
static int friend_online = 0;
static int friend_in_call = 0;
static pthread_t ffmpeg_thread_video;
static int ffmpeg_thread_video_stop = 1;
static pthread_t ffmpeg_thread_audio;
static int ffmpeg_thread_audio_stop = 1;
static pthread_t thread_key;
static int thread_key_stop = 1;
static pthread_t thread_time;
static int thread_time_stop = 1;
static const char *savedata_filename = "./savedata.tox";
static const char *savedata_tmp_filename = "./savedata.tox.tmp";
static char *global_decoder_string = "";
static char *global_encoder_string = "";
static ToxAV *toxav = NULL;
static const int global_friend_num = 0; // we always only use the first friend
AVRational time_base_audio = (AVRational) {0, 0};
AVRational time_base_video = (AVRational) {0, 0};
pthread_mutex_t time___mutex;
#define PLAY_PAUSED 0
#define PLAY_PLAYING 1
static int global_play_status = PLAY_PAUSED;
static int64_t global_pts = 0;

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

static int64_t pts_to_ms(int64_t pts, AVRational time_base)
{
    return av_rescale_q(pts, time_base, AV_TIME_BASE_Q) / 1000;
}







typedef struct {
    void* data;
    size_t size;
    size_t head;
    size_t tail;
} fifo_buffer_t;

fifo_buffer_t* fifo_buffer_create(size_t size) {
    fifo_buffer_t* buffer = calloc(1, sizeof(fifo_buffer_t));
    buffer->data = calloc(1, size);
    buffer->size = size;
    buffer->head = 0;
    buffer->tail = 0;
    return buffer;
}

void fifo_buffer_destroy(fifo_buffer_t* buffer) {
    free(buffer->data);
    free(buffer);
}

size_t fifo_buffer_free(fifo_buffer_t* buffer) {
    if ((buffer->tail == buffer->head) && (buffer->head == 0)){
        return (buffer->size - 1);
    } else {
        if (buffer->tail > 0)
        {
            if (buffer->tail < buffer->head)
            {
                // move data to beginning of the buffer
                memmove(buffer->data, buffer->data + buffer->tail, buffer->head - buffer->tail);
            }
            buffer->head -= buffer->tail;
            buffer->tail = (size_t)0;
        }
        return buffer->size - buffer->head - 1;
    }
}

size_t fifo_buffer_data_available(fifo_buffer_t* buffer) {
    if ((buffer->tail == buffer->head) && (buffer->head == (size_t)0)) {
        return (size_t)0;
    } else {
        return buffer->head - buffer->tail;
    }
}

size_t fifo_buffer_write(fifo_buffer_t* buffer, const void* data, size_t size) {
    size_t available = fifo_buffer_free(buffer);
    if (size > available) {
        size = available;
    }
    memcpy(buffer->data + buffer->head, data, size);
    buffer->head += size;
    return size;
}

size_t fifo_buffer_read(fifo_buffer_t* buffer, void* data, size_t size) {
    size_t available = fifo_buffer_data_available(buffer);
    if (size > available) {
        size = available;
    }
    memcpy(data, buffer->data + buffer->tail, size);
    buffer->tail += size;
    return size;
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
            printf("Lost connection to friend %d.\n", friend_number);
            friend_online = 0;
            break;
        case TOX_CONNECTION_TCP:
            printf("Connected to friend %d using TCP.\n", friend_number);
            friend_online = 1;
            break;
        case TOX_CONNECTION_UDP:
            printf("Connected to friend %d using UDP.\n", friend_number);
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
    if ((state & 0) || (state & 1) || (state & 2))
    {
        printf("ToxAV Call State change:fn=%d state=%d **END**\n", friend_number, state);
        friend_in_call = 0;
    }
    else
    {
        printf("ToxAV Call State change:fn=%d state=%d ok\n", friend_number, state);
        friend_in_call = 1;
    }
}

static void call_callback(ToxAV *av, uint32_t friend_number, bool audio_enabled, bool video_enabled, void *user_data)
{
    TOXAV_ERR_ANSWER error;
    bool res = toxav_answer(av, friend_number, DEFAULT_GLOBAL_AUD_BITRATE, DEFAULT_GLOBAL_VID_BITRATE, &error);
    if (error == TOXAV_ERR_ANSWER_OK)
    {
        friend_in_call = 1;
        printf("incoming ToxAV Call from fn=%d answered OK audio_enabled=%d video_enabled=%d a=%d v=%d\n",
                friend_number, (int)audio_enabled, (int)video_enabled, DEFAULT_GLOBAL_AUD_BITRATE, DEFAULT_GLOBAL_VID_BITRATE);
    }
    else
    {
        friend_in_call = 0;
        printf("incoming ToxAV Call from fn=%d **failed to answer** res=%d\n", friend_number, error);
    }
}

static void t_toxav_receive_video_frame_cb(ToxAV *av, uint32_t friend_number,
        uint16_t width, uint16_t height,
        uint8_t const *y, uint8_t const *u, uint8_t const *v,
        int32_t ystride, int32_t ustride, int32_t vstride,
        void *user_data)
{
}

static void t_toxav_receive_video_frame_pts_cb(ToxAV *av, uint32_t friend_number,
        uint16_t width, uint16_t height,
        const uint8_t *y, const uint8_t *u, const uint8_t *v,
        int32_t ystride, int32_t ustride, int32_t vstride,
        void *user_data, uint64_t pts)
{
}

static void t_toxav_receive_audio_frame_cb(ToxAV *av, uint32_t friend_number,
        int16_t const *pcm,
        size_t sample_count,
        uint8_t channels,
        uint32_t sampling_rate,
        void *user_data)
{
}

static void t_toxav_receive_audio_frame_pts_cb(ToxAV *av, uint32_t friend_number,
        int16_t const *pcm,
        size_t sample_count,
        uint8_t channels,
        uint32_t sampling_rate,
        void *user_data,
        uint64_t pts)
{
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
    else if (comm_value == TOXAV_CALL_COMM_ENCODER_CURRENT_BITRATE)
    {
        printf("ToxAV COMM: ENCODER_CURRENT_BITRATE=%ld\n", comm_number);
        if (comm_number < 400)
        {
            TOXAV_ERR_OPTION_SET error2;
            toxav_option_set(av, global_friend_num, TOXAV_ENCODER_VIDEO_MIN_BITRATE, DEFAULT_GLOBAL_VID_BITRATE, &error2);

            Toxav_Err_Bit_Rate_Set error;
            toxav_video_set_bit_rate(av, global_friend_num, DEFAULT_GLOBAL_VID_BITRATE, &error);

            TOXAV_ERR_OPTION_SET error3;
            toxav_option_set(av, global_friend_num, TOXAV_ENCODER_VIDEO_MIN_BITRATE, DEFAULT_GLOBAL_VID_BITRATE, &error3);

            printf("ToxAV COMM: setting video bitrate to %d\n", DEFAULT_GLOBAL_VID_BITRATE);
        }
    }
}
#endif



static int seek_backwards(AVFormatContext *format_ctx_seek, int stream_index)
{
    int64_t timestamp = format_ctx_seek->streams[stream_index]->start_time;
    while (timestamp < format_ctx_seek->streams[stream_index]->duration)
    {
        timestamp -= 5 * AV_TIME_BASE;
        if (timestamp < 0) {
            timestamp = 0;
        }

        if (av_seek_frame(format_ctx_seek, stream_index, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        {
            printf("Error seeking in video file\n");
            return -1;
        }

        return 0;
    }
}


static void *ffmpeg_thread_video_func(void *data)
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
    for (int i = 0; i < format_ctx->nb_streams; i++)
    {
        AVCodecParameters *codec_params = format_ctx->streams[i]->codecpar;
        AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
        if (!codec)
        {
            fprintf(stderr, "Unsupported codec!\n");
            continue;
        }
        if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index < 0)
        {
            audio_codec_ctx = avcodec_alloc_context3(codec);
            if (!audio_codec_ctx)
            {
                fprintf(stderr, "Could not allocate audio codec context\n");
                return NULL; // AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(audio_codec_ctx, codec_params)) < 0)
            {
                fprintf(stderr, "Could not copy audio codec parameters to context\n");
                return NULL; // ret;
            }
            if ((ret = avcodec_open2(audio_codec_ctx, codec, NULL)) < 0)
            {
                fprintf(stderr, "Could not open audio codec\n");
                return NULL; // ret;
            }
            audio_stream_index = i;
            audio_codec = codec;

            time_base_audio = format_ctx->streams[i]->time_base;
        }
        else if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0)
        {
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

            time_base_video = format_ctx->streams[i]->time_base;
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

    // Wait for friend to come online
    while (friend_online == 0)
    {
        yieldcpu(200);
    }

    while (friend_in_call != 1)
    {
        yieldcpu(200);
    }

    pthread_mutex_lock(&time___mutex);
    if (global_play_status == PLAY_PAUSED)
    {
        global_play_status = PLAY_PLAYING;
        fprintf(stderr, "start playing ...\n");
    }
    pthread_mutex_unlock(&time___mutex);

    while (ffmpeg_thread_video_stop != 1)
    {
        // Read packets from the input file and decode them        
        while ((av_read_frame(format_ctx, &packet) >= 0) && (ffmpeg_thread_video_stop != 1) && (friend_online != 0) && (friend_in_call == 1))
        {
            if (packet.stream_index == video_stream_index)
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
                    }
                    else if (ret < 0)
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
                    int64_t ms = pts_to_ms(pts, time_base_video); // convert PTS to milliseconds
                    // printf("PTS: %ld, Time Base: %d/%d, Milliseconds: %ld\n", pts, time_base_video.num, time_base_video.den, ms);
                    // printf("TS: %ld\n", cur_ms);

                    while ((global_play_status == PLAY_PAUSED) || (ms > global_pts))
                    {
                        usleep(1000 * 4);
                    }

                    if (toxav != NULL)
                    {
                        uint32_t frame_age_ms = 0;
                        TOXAV_ERR_SEND_FRAME error2;
                        bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                                    planes_stride[0], frame->height,
                                    dst_yuv_buffer[0], dst_yuv_buffer[1], dst_yuv_buffer[2],
                                    &error2, frame_age_ms);
                        if (error2 != TOXAV_ERR_SEND_FRAME_OK)
                        {
                            fprintf(stderr, "toxav_video_send_frame_age:%d %d\n", (int)ret2, error2);
                        }
                    }
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(&packet);
        }

        if (ffmpeg_thread_video_stop != 1)
        {
            // fprintf(stderr, "waiting for friend ...\n");
            yieldcpu(400);
        }
    }

    // Clean up
    av_frame_free(&frame);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    av_free(yuv_buffer);

    printf("ffmpeg Video Thread:Clean thread exit!\n");
    return NULL;
}



static void *ffmpeg_thread_audio_func(void *data)
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
    const int out_channels = 2; // keep in sync with `out_channel_layout`
    const int out_channel_layout = AV_CH_LAYOUT_STEREO; // AV_CH_LAYOUT_MONO or AV_CH_LAYOUT_STEREO;
    const int out_bytes_per_sample = 2; // 2 byte per PCM16 sample
    const int out_samples = 60 * 48; // X ms @ 48000Hz
    const int out_sample_rate = 48000; // fixed at 48000Hz
    const int temp_audio_buf_sizes = 50000; // fixed buffer
    fifo_buffer_t* audio_pcm_buffer = fifo_buffer_create(temp_audio_buf_sizes);

    char *inputfile = (char *)data;

    // Open the input file
    if ((ret = avformat_open_input(&format_ctx, inputfile, NULL, NULL)) < 0) {
        fprintf(stderr, "AA:Could not open input file '%s'\n", inputfile);
        return NULL; // ret;
    }

    // Retrieve stream information
    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
        fprintf(stderr, "AA:Could not find stream information\n");
        return NULL; // ret;
    }

    // Find the audio and video streams
    for (int i = 0; i < format_ctx->nb_streams; i++)
    {
        AVCodecParameters *codec_params = format_ctx->streams[i]->codecpar;
        AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
        if (!codec)
        {
            fprintf(stderr, "AA:Unsupported codec!\n");
            continue;
        }
        if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index < 0)
        {
            audio_codec_ctx = avcodec_alloc_context3(codec);
            if (!audio_codec_ctx)
            {
                fprintf(stderr, "AA:Could not allocate audio codec context\n");
                return NULL; // AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(audio_codec_ctx, codec_params)) < 0)
            {
                fprintf(stderr, "AA:Could not copy audio codec parameters to context\n");
                return NULL; // ret;
            }
            if ((ret = avcodec_open2(audio_codec_ctx, codec, NULL)) < 0)
            {
                fprintf(stderr, "AA:Could not open audio codec\n");
                return NULL; // ret;
            }
            audio_stream_index = i;
            audio_codec = codec;

            time_base_audio = format_ctx->streams[i]->time_base;
        }
        else if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0)
        {
            video_codec_ctx = avcodec_alloc_context3(codec);
            if (!video_codec_ctx) {
                fprintf(stderr, "AA:Could not allocate video codec context\n");
                return NULL; // AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(video_codec_ctx, codec_params)) < 0) {
                fprintf(stderr, "AA:Could not copy video codec parameters to context\n");
                return NULL; // ret;
            }
            if ((ret = avcodec_open2(video_codec_ctx, codec, NULL)) < 0) {
                fprintf(stderr, "AA:Could not open video codec\n");
                return NULL; // ret;
            }
            video_stream_index = i;
            video_codec = codec;

            time_base_video = format_ctx->streams[i]->time_base;
        }
    }

    // Make sure we found both audio and video streams
    if (audio_stream_index < 0 || video_stream_index < 0) {
        fprintf(stderr, "AA:Could not find audio and video streams\n");
        return NULL; // AVERROR_EXIT;
    }

    // Allocate a frame for decoding
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "AA:Could not allocate frame\n");
        return NULL; // AVERROR(ENOMEM);
    }

    swr_ctx = swr_alloc_set_opts(NULL,
                                 out_channel_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                                 audio_codec_ctx->channel_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
                                 0, NULL);
    if (!swr_ctx) {
        fprintf(stderr, "AA:Could not allocate resampler context\n");
        return NULL; // 1;
    }

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "AA:Could not initialize resampler context\n");
        return NULL; // 1;
    }

    // Wait for friend to come online
    while (friend_online == 0)
    {
        yieldcpu(200);
    }

    while (friend_in_call != 1)
    {
        yieldcpu(200);
    }

    uint8_t *buf = (uint8_t *)calloc(1, temp_audio_buf_sizes);

    while (ffmpeg_thread_audio_stop != 1)
    {
        // Read packets from the input file and decode them        
        while ((av_read_frame(format_ctx, &packet) >= 0) && (ffmpeg_thread_audio_stop != 1) && (friend_online != 0) && (friend_in_call == 1))
        {
            if (packet.stream_index == audio_stream_index)
            {
                // Decode audio packet
                ret = avcodec_send_packet(audio_codec_ctx, &packet);
                if (ret < 0)
                {
                    fprintf(stderr, "AA:Error sending audio packet for decoding\n");
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
                        fprintf(stderr, "AA:Error during audio decoding\n");
                        break;
                    }

                    num_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                    av_samples_alloc_array_and_samples(&converted_samples, NULL, out_channels, num_samples, AV_SAMPLE_FMT_S16, 0);
                    swr_convert(swr_ctx, converted_samples, num_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);

                    const int want_write_bytes = (num_samples * out_channels * out_bytes_per_sample);
                    size_t written_bytes = fifo_buffer_write(audio_pcm_buffer, converted_samples[0], want_write_bytes);
                    // printf("AA:written bytes: %ld wanted: %d\n", written_bytes, want_write_bytes);

                    // Do something with the converted samples here
                    int64_t pts = frame->pts;
                    int64_t ms = pts_to_ms(pts, time_base_video); // convert PTS to milliseconds
                    // printf("AA:PTS: %ld, Time Base: %d/%d, Milliseconds: %ld\n", pts, time_base_video.num, time_base_video.den, ms);
                    // printf("AA:TS: %ld\n", cur_ms);

                    while ((global_play_status == PLAY_PAUSED) || (ms > global_pts))
                    {
                        usleep(1000 * 4);
                    }

                    if (toxav != NULL)
                    {
                        if (fifo_buffer_data_available(audio_pcm_buffer) >= (out_samples * out_channels * out_bytes_per_sample))
                        {
                            memset(buf, 0, temp_audio_buf_sizes);
                            size_t read_bytes = fifo_buffer_read(audio_pcm_buffer, buf, out_samples * out_channels * out_bytes_per_sample);
                            // printf("AA:read_bytes: %ld\n", read_bytes);
                            Toxav_Err_Send_Frame error3;
                            toxav_audio_send_frame(toxav, global_friend_num, (const int16_t *)buf, out_samples,
                                        out_channels, frame->sample_rate, &error3);
                        }
                    }

                    av_freep(&converted_samples[0]);
                    av_freep(&converted_samples);
                }
            }
            av_packet_unref(&packet);
        }

        if (ffmpeg_thread_audio_stop != 1)
        {
            // fprintf(stderr, "waiting for friend ...\n");
            yieldcpu(400);
        }
    }

    // Clean up
    av_frame_free(&frame);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    swr_free(&swr_ctx);
    free(buf);
    fifo_buffer_destroy(audio_pcm_buffer);

    printf("ffmpeg Audio Thread:Clean thread exit!\n");
    return NULL;
}

static void *thread_time_func(void *data)
{
    pthread_mutex_lock(&time___mutex);
    global_play_status = PLAY_PAUSED;
    global_pts = 0;
    pthread_mutex_unlock(&time___mutex);

    int64_t cur_ms = 0;
    int64_t old_mono_ts = current_time_monotonic_default2();
    int first_start = 1;

    while (thread_time_stop != 1)
    {
        yieldcpu(4);
        pthread_mutex_lock(&time___mutex);
        if (global_play_status == PLAY_PLAYING)
        {
            if (first_start == 1)
            {
                first_start = 0;
                cur_ms = 0;
                global_pts = 0;
                old_mono_ts = current_time_monotonic_default2();
            }
            cur_ms = cur_ms + (int64_t)(current_time_monotonic_default2() - old_mono_ts);
            old_mono_ts = current_time_monotonic_default2();
            global_pts = cur_ms;
            // printf("TT:pts_ms=%ld\n", global_pts);
        }
        else
        {
            old_mono_ts = current_time_monotonic_default2();
        }
        pthread_mutex_unlock(&time___mutex);
    }

    printf("Key Time:Clean thread exit!\n");
    return NULL;
}


static void *thread_key_func(void *data)
{
    struct termios oldt;
    struct termios newt;
    int ch;

    while (thread_key_stop != 1)
    {
        /* Get the terminal settings */
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;

        /* Disable canonical mode and echo */
        newt.c_lflag &= ~(ICANON | ECHO);

        /* Set the new terminal settings */
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        /* Wait for a keypress */
        while ((ch = getchar()) != ' ') {}

        pthread_mutex_lock(&time___mutex);
        if (global_play_status == PLAY_PAUSED)
        {
            global_play_status = PLAY_PLAYING;
            printf("KK:----- PLAY  -----\n");
        }
        else if (global_play_status == PLAY_PLAYING)
        {
            global_play_status = PLAY_PAUSED;
            printf("KK:----- PAUSE -----\n");
        }
        pthread_mutex_unlock(&time___mutex);

    }
    /* Restore the old terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    printf("Key Thread:Clean thread exit!\n");
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

    if (pthread_mutex_init(&time___mutex, NULL) != 0)
    {
        fprintf(stderr, "Creating mutex failed\n");
        return 0;
    }

    global_play_status = PLAY_PAUSED;
    global_pts = 0;

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
    friend_in_call = 0;

#ifndef TOX_HAVE_TOXUTIL
    printf("init Tox\n");
    Tox *tox = tox_new(&options, NULL);
#else
    printf("init Tox [TOXUTIL]\n");
    Tox *tox = tox_utils_new(&options, NULL);
#endif
    printf("init ToxAV\n");
    toxav = toxav_new(tox, NULL);

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
    toxav_callback_video_receive_frame(toxav, t_toxav_receive_video_frame_cb, NULL);
    toxav_callback_video_receive_frame_pts(toxav, t_toxav_receive_video_frame_pts_cb, NULL);
    toxav_callback_audio_receive_frame(toxav, t_toxav_receive_audio_frame_cb, NULL);
    toxav_callback_audio_receive_frame_pts(toxav, t_toxav_receive_audio_frame_pts_cb, NULL);
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



    ffmpeg_thread_video_stop = 0;
    if (pthread_create(&ffmpeg_thread_video, NULL, ffmpeg_thread_video_func, (void *)argv[1]) != 0)
    {
        printf("ffmpeg Video Thread create failed\n");
    }
    else
    {
        pthread_setname_np(ffmpeg_thread_video, "t_ffmpeg_v");
        printf("ffmpeg Video Thread successfully created\n");
    }


    ffmpeg_thread_audio_stop = 0;
    if (pthread_create(&ffmpeg_thread_audio, NULL, ffmpeg_thread_audio_func, (void *)argv[1]) != 0)
    {
        printf("ffmpeg Audio Thread create failed\n");
    }
    else
    {
        pthread_setname_np(ffmpeg_thread_audio, "t_ffmpeg_a");
        printf("ffmpeg Audio Thread successfully created\n");
    }

    thread_time_stop = 0;
    if (pthread_create(&thread_time, NULL, thread_time_func, (void *)argv[1]) != 0)
    {
        printf("Time Thread create failed\n");
    }
    else
    {
        pthread_setname_np(thread_time, "t_time");
        printf("Time Thread successfully created\n");
    }

    thread_key_stop = 0;
    if (pthread_create(&thread_key, NULL, thread_key_func, (void *)argv[1]) != 0)
    {
        printf("Key Thread create failed\n");
    }
    else
    {
        pthread_setname_np(thread_key, "t_key");
        printf("Key Thread successfully created\n");
    }


    // ----------- main loop -----------
    while (1 == 1)
    {
        toxav_iterate(toxav);
        yieldcpu(2);
        tox_iterate(tox, NULL);
        yieldcpu(8);
    }
    // ----------- main loop -----------

    // Clean up
    thread_key_stop = 1;
    pthread_join(thread_key, NULL);

    ffmpeg_thread_audio_stop = 1;
    pthread_join(ffmpeg_thread_audio, NULL);

    ffmpeg_thread_video_stop = 1;
    pthread_join(ffmpeg_thread_video, NULL);

    thread_time_stop = 1;
    pthread_join(thread_time, NULL);

    pthread_mutex_destroy(&time___mutex);

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

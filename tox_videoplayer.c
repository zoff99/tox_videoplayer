/**
 *
 * tox_videoplayer
 * (C)Zoff <zoff@zoff.cc> in 2023
 *
 * https://github.com/zoff99/tox_videoplayer
 *
 *
 */
/*
 * Copyright © 2023 Zoff <zoff@zoff.cc>
 *
 * tox_videoplayer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tox_videoplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <https://www.gnu.org/licenses/>.
 */

/*
 *

 ASAN compile:
 gcc -O3 -g -fno-omit-frame-pointer -fsanitize=address -static-libasan -fPIC -Wno-discarded-qualifiers tox_videoplayer.c $(pkg-config --cflags --libs x11 libsodium libswresample opus vpx libavcodec libswscale libavformat libavdevice libavutil x264) -pthread -o tox_videoplayer

 NORMAL compile:
 gcc -O3 -flto -fomit-frame-pointer -g -march=native -fPIC -Wno-discarded-qualifiers tox_videoplayer.c $(pkg-config --cflags --libs x11 libsodium libswresample opus vpx libavcodec libswscale libavformat libavdevice libavutil x264) -pthread -o tox_videoplayer

 MOSTLY STATIC compile:
 gcc -O3 -fomit-frame-pointer -Wno-discarded-qualifiers -fPIC tox_videoplayer.c \
  -Wl,-Bstatic \
  $(pkg-config --cflags --libs libsodium x264) \
  -Wl,-Bdynamic \
  $(pkg-config --cflags --libs x11 libswresample opus vpx libavcodec libswscale libavformat libavdevice libavutil ) \
  -pthread -o tox_videoplayer

 * 
 */

#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)
#define _FILE_OFFSET_BITS 64 // NOLINT(bugprone-reserved-identifier)
#define _LARGEFILE_SOURCE // NOLINT(bugprone-reserved-identifier)

#include <ctype.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <X11/Xlib.h>

#include <sodium/utils.h>

// define this before including toxcore amalgamation -------
#define MIN_LOGGER_LEVEL LOGGER_LEVEL_WARNING // LOGGER_LEVEL_DEBUG
#define HW_CODEC_CONFIG_UTOX_UB81
// #define HW_CODEC_CONFIG_TBW_LINNVENC
#define HW_CODEC_CONFIG_HIGHVBITRATE
// define this before including toxcore amalgamation -------

// include toxcore amalgamation with ToxAV --------
#include "toxcore_amalgamation.c"
// include toxcore amalgamation with ToxAV --------


#include "shell_percentage_meter.h"


// ----------- version -----------
// ----------- version -----------
#define TOX_VPLAYER_VERSION_MAJOR 0
#define TOX_VPLAYER_VERSION_MINOR 99
#define TOX_VPLAYER_VERSION_PATCH 5
static const char global_tox_vplayer_version_string[] = "0.99.5";

// ----------- version -----------
// ----------- version -----------

#define DEFAULT_GLOBAL_AUD_BITRATE 128 // kbit/sec
#define DEFAULT_GLOBAL_VID_BITRATE 8000 // kbit/sec
int GLOBAL_VID_BITRATE_var = DEFAULT_GLOBAL_VID_BITRATE;
int vbr = 0;
int max_video_bitrate = 8000;
int max_video_bitrate_set = 0;
#define DEFAULT_SCREEN_CAPTURE_FPS "30" // 30 fps desktop screen capture
#define DEFAULT_SCREEN_CAPTURE_PULSE_DEVICE "default" // default pulse device is called "default"

static int self_online = 0;
static int friend_online = 0;
static int friend_in_call = 0;
static int switch_tcponly = 0;
static bool show_progress_bar = false;
#define PROXY_PORT_TOR_DEFAULT 9050
static int use_tor = 0;
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
char *global_pulse_inputdevice_name = NULL;
char *global_desktop_capture_fps = NULL;
int global_osd_message_toggle = 0;
int global_hdmifreq_toggle = 60;
static bool main_loop_running;
int need_free_global_pulse_inputdevice_name = 0;
int need_free_global_desktop_capture_fps = 0;
AVRational time_base_audio = (AVRational) {0, 0};
AVRational time_base_video = (AVRational) {0, 0};
int64_t audio_start_time = 0;
int global_audio_delay_factor = 0;
int global_video_delay_factor = 7; // default video delay of 7 * 50 (= 350m) to audio
pthread_mutex_t time___mutex;
pthread_mutex_t vscale___mutex;
pthread_mutex_t vsend___mutex;
int vsend_thread_count = 0;
#define PLAY_PAUSED 0
#define PLAY_PLAYING 1
static int global_need_video_seek = 0;
static int global_need_audio_seek = 0;
static int global_play_status = PLAY_PAUSED;
static int64_t global_pts = 0;
int64_t global_time_cur_ms = 0;
int64_t global_time_old_mono_ts = 0;
const int seek_delta_ms = 30 * 1000; // seek X seconds
const int seek_delta_ms_faster = 5 * 60 * 1000; // seek X minutes
struct termios oldt;
struct termios newt;


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

struct Node1 {
    char *ip;
    char *key;
    uint16_t udp_port;
    uint16_t tcp_port;
} nodes1[] = {
{ "2604:a880:1:20::32f:1001", "BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F", 33445, 33445 },
{ "46.101.197.175", "CD133B521159541FB1D326DE9850F5E56A6C724B5B8E5EB5CD8D950408E95707", 33445, 3389 },
{ "144.217.167.73","7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C",33445,33445},
{ "tox1.mf-net.eu", "B3E5FA80DC8EBD1149AD2AB35ED8B85BD546DEDE261CA593234C619249419506", 33445, 3389 },
{ "bg.tox.dcntrlzd.network", "20AD2A54D70E827302CDF5F11D7C43FA0EC987042C36628E64B2B721A1426E36", 33445, 33445 },
{"91.219.59.156","8E7D0B859922EF569298B4D261A8CCB5FEA14FB91ED412A7603A585A25698832",33445,33445},
{"85.143.221.42","DA4E4ED4B697F2E9B000EEFE3A34B554ACD3F45F5C96EAEA2516DD7FF9AF7B43",33445,33445},
{"tox.initramfs.io","3F0A45A268367C1BEA652F258C85F4A66DA76BCAA667A49E770BCC4917AB6A25",33445,33445},
{"144.217.167.73","7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C",33445,33445},
{"tox.abilinski.com","10C00EB250C3233E343E2AEBA07115A5C28920E9C8D29492F6D00B29049EDC7E",33445,33445},
{"tox.novg.net","D527E5847F8330D628DAB1814F0A422F6DC9D0A300E6C357634EE2DA88C35463",33445,33445},
{"198.199.98.108","BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F",33445,33445},
{"tox.kurnevsky.net","82EF82BA33445A1F91A7DB27189ECFC0C013E06E3DA71F588ED692BED625EC23",33445,33445},
{"81.169.136.229","E0DB78116AC6500398DDBA2AEEF3220BB116384CAB714C5D1FCD61EA2B69D75E",33445,33445},
{"205.185.115.131","3091C6BEB2A993F1C6300C16549FABA67098FF3D62C6D253828B531470B53D68",53,53},
{"bg.tox.dcntrlzd.network","20AD2A54D70E827302CDF5F11D7C43FA0EC987042C36628E64B2B721A1426E36",33445,33445},
{"46.101.197.175","CD133B521159541FB1D326DE9850F5E56A6C724B5B8E5EB5CD8D950408E95707",33445,33445},
{"tox1.mf-net.eu","B3E5FA80DC8EBD1149AD2AB35ED8B85BD546DEDE261CA593234C619249419506",33445,33445},
{"tox2.mf-net.eu","70EA214FDE161E7432530605213F18F7427DC773E276B3E317A07531F548545F",33445,33445},
{"195.201.7.101","B84E865125B4EC4C368CD047C72BCE447644A2DC31EF75BD2CDA345BFD310107",33445,33445},
{"tox4.plastiras.org","836D1DA2BE12FE0E669334E437BE3FB02806F1528C2B2782113E0910C7711409",33445,33445},
{"gt.sot-te.ch","F4F4856F1A311049E0262E9E0A160610284B434F46299988A9CB42BD3D494618",33445,33445},
{"188.225.9.167","1911341A83E02503AB1FD6561BD64AF3A9D6C3F12B5FBB656976B2E678644A67",33445,33445},
{"122.116.39.151","5716530A10D362867C8E87EE1CD5362A233BAFBBA4CF47FA73B7CAD368BD5E6E",33445,33445},
{"195.123.208.139","534A589BA7427C631773D13083570F529238211893640C99D1507300F055FE73",33445,33445},
{"tox3.plastiras.org","4B031C96673B6FF123269FF18F2847E1909A8A04642BBECD0189AC8AEEADAF64",33445,33445},
{"104.225.141.59","933BA20B2E258B4C0D475B6DECE90C7E827FE83EFA9655414E7841251B19A72C",43334,43334},
{"139.162.110.188","F76A11284547163889DDC89A7738CF271797BF5E5E220643E97AD3C7E7903D55",33445,33445},
{"198.98.49.206","28DB44A3CEEE69146469855DFFE5F54DA567F5D65E03EFB1D38BBAEFF2553255",33445,33445},
{"172.105.109.31","D46E97CF995DC1820B92B7D899E152A217D36ABE22730FEA4B6BF1BFC06C617C",33445,33445},
{"ru.tox.dcntrlzd.network","DBB2E896990ECC383DA2E68A01CA148105E34F9B3B9356F2FE2B5096FDB62762",33445,33445},
{"91.146.66.26","B5E7DAC610DBDE55F359C7F8690B294C8E4FCEC4385DE9525DBFA5523EAD9D53",33445,33445},
{"tox01.ky0uraku.xyz","FD04EB03ABC5FC5266A93D37B4D6D6171C9931176DC68736629552D8EF0DE174",33445,33445},
{"tox02.ky0uraku.xyz","D3D6D7C0C7009FC75406B0A49E475996C8C4F8BCE1E6FC5967DE427F8F600527",33445,33445},
{"tox.plastiras.org","8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725",33445,33445},
{"kusoneko.moe","BE7ED53CD924813507BA711FD40386062E6DC6F790EFA122C78F7CDEEE4B6D1B",33445,33445},
{"tox2.plastiras.org","B6626D386BE7E3ACA107B46F48A5C4D522D29281750D44A0CBA6A2721E79C951",33445,33445},
{"172.104.215.182","DA2BD927E01CD05EBCC2574EBE5BEBB10FF59AE0B2105A7D1E2B40E49BB20239",33445,33445},
    { NULL, NULL, 0, 0 }
};

/**
 * @brief Converts a hexadecimal string to binary format
 *
 * @param hex_string The hexadecimal string to be converted, must be NULL terminated
 * @param output Pointer to the binary format output buffer
 */
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

/**
 * @brief Converts binary data to uppercase hexadecimal string using libsodium
 *
 * @param bin Pointer to binary data
 * @param bin_size Size of binary data
 * @param hex Pointer to hexadecimal string
 * @param hex_size Size of hexadecimal string
 */
static void bin2upHex(const uint8_t *bin, uint32_t bin_size, char *hex, uint32_t hex_size)
{
    sodium_bin2hex(hex, hex_size, bin, bin_size);

    for (size_t i = 0; i < hex_size - 1; i++) {
        hex[i] = toupper(hex[i]);
    }
}

/**
 * @brief This function returns a counter value that increases every millisecond using the CLOCK_MONOTONIC clock.
 *
 * @return uint64_t The current counter value in milliseconds.
 */
static uint64_t current_time_monotonic_default2(void)
{
    struct timespec clock_mono;
    clock_gettime(CLOCK_MONOTONIC, &clock_mono);
    uint64_t time = 1000ULL * clock_mono.tv_sec + (clock_mono.tv_nsec / 1000000ULL);
    return time;
}

/**
 * @brief Delays the execution of the current thread for a specified number of milliseconds.
 *
 * @param ms The number of milliseconds to delay the execution of the current thread.
 */
static void yieldcpu(uint32_t ms)
{
    usleep(1000 * ms);
}

/**
 * @brief Get the current Unix time.
 *
 * @return The time as the number of seconds since the Epoch.
 */
static time_t get_unix_time(void)
{
    return time(NULL);
}

/**
 * @brief Calculates the percentage of the current value relative to the maximum value.
 *
 * @param current_value The current value.
 * @param max_value The maximum value.
 * @return The percentage of the current value relative to the maximum value.
 */
static float calculate_percentage(int64_t current_value, int64_t max_value)
{
    float percentage = ((float)current_value / (float)max_value) * 100.0;
    return percentage;
}

/**
 * @brief Convert presentation timestamp (pts) to milliseconds (ms)
 *
 * @param pts Presentation timestamp
 * @param time_base Time base
 * @return Converted value in milliseconds
 */
static int64_t pts_to_ms(int64_t pts, AVRational time_base)
{
    return av_rescale_q(pts, time_base, AV_TIME_BASE_Q) / 1000;
}

/**
 * @brief Changes the terminal settings to disable canonical mode and echo.
 *
 */
static void change_term()
{
    printf("change_term\n");
    /* Get the terminal settings */
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    /* Disable canonical mode and echo */
    newt.c_lflag &= ~(ICANON | ECHO);
    /* Set the new terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

/**
 * @brief Restores the old terminal settings
 *
 */
static void restore_term()
{
    printf("restore_term\n");
    /* Restore the old terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static void draw_percent_bar(int percent, bool blocked)
{
    if (show_progress_bar)
    {
        __shell_percentage__draw_progress_bar(percent, blocked);
        setvbuf(stdout, NULL, _IONBF, 0);
        printf("%s", __shell_percentage__RESTORE_FG);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
}

// ############## FIFO ##############
// ############## FIFO ##############

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

// ############## FIFO ##############
// ############## FIFO ##############

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
    bool res = toxav_answer(av, friend_number, DEFAULT_GLOBAL_AUD_BITRATE, GLOBAL_VID_BITRATE_var, &error);
    if (error == TOXAV_ERR_ANSWER_OK)
    {
        friend_in_call = 1;
        printf("incoming ToxAV Call from fn=%d answered OK audio_enabled=%d video_enabled=%d a=%d v=%d\n",
                friend_number, (int)audio_enabled, (int)video_enabled, DEFAULT_GLOBAL_AUD_BITRATE, GLOBAL_VID_BITRATE_var);
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
        if ((vbr == 1) && (max_video_bitrate != 8000) && (max_video_bitrate_set == 0))
        {
            max_video_bitrate_set = 1;
            TOXAV_ERR_OPTION_SET error2;
            toxav_option_set(av, global_friend_num, TOXAV_ENCODER_VIDEO_MAX_BITRATE, max_video_bitrate, &error2);
            printf("ToxAV COMM: setting max. video bitrate to %d\n", max_video_bitrate);
        }
        else
        {
            if (comm_number < 400)
            {
                if ((vbr == 0) && (max_video_bitrate == 8000))
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
    }
}
#endif

/**
 *
 * 8x8 monochrome bitmap fonts for rendering
 * Author: Daniel Hepper <daniel@hepper.net>
 *
 * https://github.com/dhepper/font8x8
 *
 * License: Public Domain
 *
 **/
// Constant: font8x8_basic
// Contains an 8x8 font map for unicode points U+0000 - U+007F (basic latin)
char font8x8_basic[128][8] =
{
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (//)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
};

// "0" -> [48]
// "9" -> [57]
// ":" -> [58]
/**
 * @brief Get the bitmap of a character from a font
 *
 * @param font_char_num The character number to get the bitmap from
 * @return char* The bitmap of the character
 */
static char *get_bitmap_from_font(int font_char_num)
{
    char *ret_bitmap = font8x8_basic[0x3F]; // fallback: "?"

    if ((font_char_num >= 0) && (font_char_num <= 0x7F))
    {
        ret_bitmap = font8x8_basic[font_char_num];
    }

#if 0
    else if ((font_char_num >= 0xA0) && (font_char_num <= 0xFF))
    {
        ret_bitmap = font8x8_ext_latin[font_char_num - 0xA0];
    }

#endif
    return ret_bitmap;
}

/**
 * @brief Prints a character from a font to a given location in a Y-plane buffer of a YUV display
 *
 * @param start_x_pix Starting x-coordinate of the character on the display
 * @param start_y_pix Starting y-coordinate of the character on the display
 * @param font_char_num The character number in the font to be printed
 * @param col_value The color value to be used for the character
 * @param yy Pointer to the y-plane of the display
 * @param w Width of the display
 */
void print_font_char_ptr(int start_x_pix, int start_y_pix, int font_char_num,
                         uint8_t col_value, uint8_t *yy, int w)
{
    int font_w = 8;
    int font_h = 8;
    uint8_t *y_plane = yy;
    // uint8_t col_value = 0; // black
    char *bitmap = get_bitmap_from_font(font_char_num);
    int k;
    int j;
    int offset = 0;
    int set = 0;

    for (k = 0; k < font_h; k++)
    {
        y_plane = yy + ((start_y_pix + k) * w);
        y_plane = y_plane + start_x_pix;

        for (j = 0; j < font_w; j++)
        {
            set = bitmap[k] & 1 << j;

            if (set)
            {
                *y_plane = col_value; // set luma value
            }

            y_plane = y_plane + 1;
        }
    }
}

#define UTF8_EXTENDED_OFFSET 64

static void text_on_yuf_frame_xy_ptr(int start_x_pix, int start_y_pix, const char *text, uint8_t col_value, uint8_t *yy,
                              int w, int h)
{
    int carriage = 0;
    const int letter_width = 8;
    const int letter_spacing = 1;
    int block_needed_width = 2 + 2 + (strlen(text) * (letter_width + letter_spacing));
    int looper;
    bool lat_ext = false;

    for (looper = 0; (int)looper < (int)strlen(text); looper++)
    {
        uint8_t c = text[looper];

        if (((c > 0) && (c < 127)) || (lat_ext == true))
        {
            if (lat_ext == true)
            {
                print_font_char_ptr((2 + start_x_pix + ((letter_width + letter_spacing) * carriage)),
                                    2 + start_y_pix, c + UTF8_EXTENDED_OFFSET, letter_width, yy, w);
            }
            else
            {
                print_font_char_ptr((2 + start_x_pix + ((letter_width + letter_spacing) * carriage)),
                                    2 + start_y_pix, c, col_value, yy, w);
            }

            lat_ext = false;
        }
        else
        {
            // leave a blank
            if (c == 0xC3)
            {
                // UTF-8 latin extended
                lat_ext = true;
            }
            else
            {
                lat_ext = false;
            }

            carriage--;
        }

        carriage++;
    }
}

static void show_seek_forward()
{

#define SEEK_YUV_WIDTH 64
#define SEEK_YUV_HEIGHT 64

    const int color_white = 128;
    const int color_black = 16;
    const int font_height = 8;
    // Allocate memory for the YUV image buffer
    unsigned char *yuv_image = (unsigned char*) calloc(1, SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT * 3 / 2 * sizeof(unsigned char));

    // Set the Y component of the image to black
    for (int i = 0; i < SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT; i++) {
        yuv_image[i] = color_black;
    }

    // Set the U and V components of the image to 128
    for (int i = SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT; i < SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT * 3 / 2; i++) {
        yuv_image[i] = color_white;
    }

    text_on_yuf_frame_xy_ptr(1, ((SEEK_YUV_HEIGHT / 2) - (font_height / 2) - 2),
        "SEEK >>", color_white, yuv_image, SEEK_YUV_WIDTH, SEEK_YUV_HEIGHT);

    bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                SEEK_YUV_WIDTH, SEEK_YUV_HEIGHT,
                yuv_image,
                yuv_image + (SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT),
                yuv_image + (SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT) + ((SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT) / 4),
                NULL, 0);

    // Free the memory allocated for the YUV image buffer
    free(yuv_image);
}

static void show_pause(int age_ms)
{

#define PAUSE_YUV_WIDTH 64
#define PAUSE_YUV_HEIGHT 64

    const int color_white = 128;
    const int color_black = 16;
    const int font_height = 8;
    // Allocate memory for the YUV image buffer
    unsigned char *yuv_image = (unsigned char*) calloc(1, PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT * 3 / 2 * sizeof(unsigned char));

    // Set the Y component of the image to black
    for (int i = 0; i < PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT; i++) {
        yuv_image[i] = color_black;
    }

    // Set the U and V components of the image to 128
    for (int i = PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT; i < PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT * 3 / 2; i++) {
        yuv_image[i] = color_white;
    }

    text_on_yuf_frame_xy_ptr(0, ((PAUSE_YUV_HEIGHT / 2) - (font_height / 2) - 2),
        "PAUSE||", color_white, yuv_image, PAUSE_YUV_WIDTH, PAUSE_YUV_HEIGHT);

    TOXAV_ERR_SEND_FRAME error2;
    bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                PAUSE_YUV_WIDTH, PAUSE_YUV_HEIGHT,
                yuv_image,
                yuv_image + (PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT),
                yuv_image + (PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT) + ((PAUSE_YUV_WIDTH * PAUSE_YUV_HEIGHT) / 4),
                &error2, age_ms);
    if (error2 != TOXAV_ERR_SEND_FRAME_OK)
    {
        // fprintf(stderr, "show_pause:%d %d\n", (int)ret2, error2);
    }

    // Free the memory allocated for the YUV image buffer
    free(yuv_image);
}

static void show_novideo_text(int age_ms)
{

#define NOVIDEO_YUV_WIDTH 640
#define NOVIDEO_YUV_HEIGHT 480

    const int color_white = 128;
    const int color_black = 16;
    const int font_height = 8;
    unsigned char *yuv_image = (unsigned char*) calloc(1, NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT * 3 / 2 * sizeof(unsigned char));
    for (int i = 0; i < NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT; i++) {
        yuv_image[i] = color_black;
    }
    for (int i = NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT; i < NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT * 3 / 2; i++) {
        yuv_image[i] = color_white;
    }

    text_on_yuf_frame_xy_ptr(0, ((NOVIDEO_YUV_HEIGHT / 2) - (font_height / 2) - 2),
        "NO VIDEO", color_white, yuv_image, NOVIDEO_YUV_WIDTH, NOVIDEO_YUV_HEIGHT);

    TOXAV_ERR_SEND_FRAME error2;
    bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                NOVIDEO_YUV_WIDTH, NOVIDEO_YUV_HEIGHT,
                yuv_image,
                yuv_image + (NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT),
                yuv_image + (NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT) + ((NOVIDEO_YUV_WIDTH * NOVIDEO_YUV_HEIGHT) / 4),
                &error2, age_ms);
    if (error2 != TOXAV_ERR_SEND_FRAME_OK)
    {
        fprintf(stderr, "show_novideo_text:%d %d\n", (int)ret2, error2);
    }
    free(yuv_image);
}

static void flush_video(int age_ms)
{

#define SEEK_YUV_WIDTH 64
#define SEEK_YUV_HEIGHT 64
    const int color_white = 128;
    const int color_black = 16;
    const int font_height = 8;
    unsigned char *yuv_image = (unsigned char*) calloc(1, SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT * 3 / 2 * sizeof(unsigned char));
    for (int i = 0; i < SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT; i++) {
        yuv_image[i] = color_black;
    }
    for (int i = SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT; i < SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT * 3 / 2; i++) {
        yuv_image[i] = color_white;
    }
    bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                SEEK_YUV_WIDTH, SEEK_YUV_HEIGHT,
                yuv_image,
                yuv_image + (SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT),
                yuv_image + (SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT) + ((SEEK_YUV_WIDTH * SEEK_YUV_HEIGHT) / 4),
                NULL, age_ms);
    free(yuv_image);
}

/**
 * @brief Seeks to a specific timestamp within a given stream index.
 *
 * @param format_ctx_seek The format context to seek within.
 * @param codec_context The codec context to flush buffers for.
 * @param stream_index The index of the stream to seek within.
 * @return int Returns 0 on success, or an error code on failure.
 */
static int seek_stream(AVFormatContext *format_ctx_seek, AVCodecContext *codec_context, int stream_index)
{
    int64_t cur_pos;
    pthread_mutex_lock(&time___mutex);
    cur_pos = global_pts;
    pthread_mutex_unlock(&time___mutex);

    int64_t timestamp = format_ctx_seek->streams[stream_index]->start_time;

    // convert seconds provided by the user to a timestamp in a correct base,
    // then save it for later.
    int64_t m_target_ts = av_rescale_q(((cur_pos / 1000) + 0) * AV_TIME_BASE, AV_TIME_BASE_Q,
        format_ctx_seek->streams[stream_index]->time_base);

    printf("seek_stream:start time=%ld cur_pos=%ld sec_to_seek=%d m_target_ts=%ld\n",
            timestamp, cur_pos, 0, m_target_ts);
    avcodec_flush_buffers(codec_context);

    // Here we seek within given stream index and the correct timestamp 
    // for that stream. Using AVSEEK_FLAG_BACKWARD to make sure we're 
    // always *before* requested timestamp.
    int err;
    err = av_seek_frame(format_ctx_seek, stream_index, m_target_ts, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
    fprintf(stderr, "seeking result: %s\n", av_err2str(err));
    return 0;
}

/**
 * @brief Prints audio codec parameters
 *
 * @param codecpar AVCodecParameters pointer to the audio codec parameters
 * @param text_prefix Prefix to add to the printed text
 */
static void print_codec_parameters_audio(AVCodecParameters *codecpar, const char* text_prefix) {
    printf("%s===================================\n", text_prefix);
    printf("%sCodec Type: %s\n", text_prefix, avcodec_get_name(codecpar->codec_id));
    printf("%sCodec ID: %d\n", text_prefix, codecpar->codec_id);
    if (av_get_sample_fmt_name(codecpar->format) != NULL)
    {
        printf("%sSample Format: %s\n", text_prefix, av_get_sample_fmt_name(codecpar->format));
    }
    printf("%sSample Rate: %d\n", text_prefix, codecpar->sample_rate);
    printf("%sChannels: %d\n", text_prefix, codecpar->channels);
    printf("%sChannel Layout: %ld\n", text_prefix, codecpar->channel_layout);
    printf("%sBit Rate: %ld\n", text_prefix, codecpar->bit_rate);
    printf("%sBlock Align: %d\n", text_prefix, codecpar->block_align);
    printf("%sFrame Size: %d\n", text_prefix, codecpar->frame_size);
    printf("%sInitial Padding: %d\n", text_prefix, codecpar->initial_padding);
    printf("%sTrailing Padding: %d\n", text_prefix, codecpar->trailing_padding);
    printf("%sSeek Preroll: %d\n", text_prefix, codecpar->seek_preroll);
    printf("%s===================================\n", text_prefix);
}

/**
 * @brief Prints video codec parameters
 *
 * @param codecpar AVCodecParameters pointer to the video codec parameters
 * @param text_prefix Prefix to add to the printed text
 */
static void print_codec_parameters_video(AVCodecParameters *codecpar, const char* text_prefix) {
    AVCodecContext *codec = avcodec_alloc_context3(NULL);
    if (codec == NULL)
    {
        return;
    }
    printf("%s===================================\n", text_prefix);
    int res = avcodec_parameters_to_context(codec, codecpar);
    if (res >= 0)
    {
        printf("%sCodec Type: %s\n", text_prefix, avcodec_get_name(codec->codec_id));
        if ((codec->codec != NULL) && (av_get_profile_name(codec->codec, codec->profile) != NULL))
        {
            printf("%sCodec Profile: %s\n", text_prefix, av_get_profile_name(codec->codec, codec->profile));
        }
        printf("%sCodec Level: %d\n", text_prefix, codec->level);
        printf("%sCodec Width: %d\n", text_prefix, codec->width);
        printf("%sCodec Height: %d\n", text_prefix, codec->height);
        printf("%sCodec Bitrate: %ld\n", text_prefix, codec->bit_rate);
        if (av_get_pix_fmt_name(codec->pix_fmt) != NULL)
        {
            printf("%sCodec Format: %s\n", text_prefix, av_get_pix_fmt_name(codec->pix_fmt));
        }
    }
    printf("%s===================================\n", text_prefix);
    avcodec_free_context(&codec);
}

/**
 * @brief Calculates the bounding box to full HD resolution.
 *
 * @param in_width The input width.
 * @param in_height The input height.
 * @param out_width The output width.
 * @param out_height The output height.
 */
static void calculateBoundingBox_to_fullhd(int in_width, int in_height, int *out_width, int *out_height)
{
    const double aspectRatio = (double)in_width / (double)in_height;
    if (aspectRatio > (16.0 / 9.0)) {
        *out_width = 1920;
        *out_height = (int)(1920 / aspectRatio);
    } else {
        *out_height = 1080;
        *out_width = (int)(1080 * aspectRatio);
    }
}

struct vsend_data {
    int output_width;
    int output_height;
    struct SwsContext *scaler_ctx;
    AVFrame* frame2;
    AVCodecContext *video_codec_ctx;
    AVFormatContext *format_ctx;
    int64_t video_start_time;
    int desktop_mode;
    int http_mode;
    int64_t video_length;
    int video_position_percent;
    int video_stream_index;
    int64_t ms_desktop_pin;
    uint8_t *dst_yuv_buffer[3];
};

static void *thread_v_send_bg_func(void *data)
{
    struct vsend_data *vs = (struct vsend_data *) data;
    int output_width = vs->output_width;
    int output_height = vs->output_height;
    struct SwsContext *scaler_ctx = vs->scaler_ctx;
    AVFrame* frame2 = vs->frame2;
    AVCodecContext *video_codec_ctx = vs->video_codec_ctx;
    AVFormatContext *format_ctx = vs->format_ctx;
    int64_t video_start_time = vs->video_start_time;
    int desktop_mode = vs->desktop_mode;
    int http_mode = vs->http_mode;
    int64_t video_length = vs->video_length;
    int video_position_percent = vs->video_position_percent;
    int video_stream_index = vs->video_stream_index;
    int64_t ms_desktop_pin = vs->ms_desktop_pin;

    // Convert the video frame to YUV
    int planes_stride[3];
    planes_stride[0] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 0);
    planes_stride[1] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 1);
    planes_stride[2] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 2);

    uint8_t *yuv_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
            output_width, output_height, 1));
    if (yuv_buffer == NULL) {
        fprintf(stderr, "Error: could not allocate YUV buffer\n");

        av_frame_unref(frame2);
        free(vs);
        pthread_mutex_lock(&vsend___mutex);
        vsend_thread_count--;
        if (vsend_thread_count < 0)
        {
            vsend_thread_count = 0;
        }
        pthread_mutex_unlock(&vsend___mutex);

        return NULL;
    }

    uint8_t *dst_yuv_buffer[3];
    dst_yuv_buffer[0] = yuv_buffer;
    dst_yuv_buffer[1] = yuv_buffer + (output_width * output_height);
    dst_yuv_buffer[2] = dst_yuv_buffer[1] + ((output_width * output_height) / 4);

    pthread_mutex_lock(&vscale___mutex);
    sws_scale(scaler_ctx, (const uint8_t * const*)frame2->data, frame2->linesize, 0, video_codec_ctx->height,
            dst_yuv_buffer, planes_stride);
    pthread_mutex_unlock(&vscale___mutex);

    int64_t ms = global_pts;

    int counter = 0;
    const int sleep_ms = 4;
    const int one_sec_ms = 1000;
    int delay_add = 0;

    if (toxav != NULL)
    {
        if (global_play_status == PLAY_PLAYING)
        {
            uint32_t frame_age_ms = global_pts - ms_desktop_pin;
            if (frame_age_ms < 0)
            {
                frame_age_ms = 0;
            }
            else if (frame_age_ms > 1000)
            {
                frame_age_ms = 1000;
            }
            // fprintf(stderr, "frame age:%d\n", frame_age_ms);


            TOXAV_ERR_SEND_FRAME error2;
            bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                        planes_stride[0], output_height,
                        dst_yuv_buffer[0], dst_yuv_buffer[1], dst_yuv_buffer[2],
                        &error2, frame_age_ms);

            if (error2 != TOXAV_ERR_SEND_FRAME_OK)
            {
                fprintf(stderr, "toxav_video_send_frame_age:%d %d -> retrying ...\n", (int)ret2, error2);
                yieldcpu(1);
                frame_age_ms = 1;
                ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                            planes_stride[0], output_height,
                            dst_yuv_buffer[0], dst_yuv_buffer[1], dst_yuv_buffer[2],
                            &error2, frame_age_ms);
                if (error2 != TOXAV_ERR_SEND_FRAME_OK)
                {
                    fprintf(stderr, "toxav_video_send_frame_age:%d %d -> retrying -> FAILED\n", (int)ret2, error2);
                }
            }
        }
        else
        {
            // global_play_status == PLAY_PAUSED
        }
    }

    av_free(yuv_buffer);
    av_frame_unref(frame2);
    free(vs);
    pthread_mutex_lock(&vsend___mutex);
    vsend_thread_count--;
    if (vsend_thread_count < 0)
    {
        vsend_thread_count = 0;
    }
    pthread_mutex_unlock(&vsend___mutex);
    return NULL;
}

static void *ffmpeg_thread_video_func(void *data)
{
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodec *video_codec = NULL;
    AVPacket packet;
    AVFrame *frame = NULL;
    int video_stream_index = -1;
    int64_t video_start_time = 0;
    int64_t video_av_starttime = 0;
    int64_t video_duration = 0;
    int64_t video_length = 0;
    int64_t video_time_base_den = 0;
    int video_position_percent = -1;
    int num_samples;
    int output_width = 0;
    int output_height = 0;
    uint8_t **converted_samples = NULL;
    int ret;
    int64_t ms_desktop_pin = 0;
    int desktop_mode = 0;
    int http_mode = 0;

    char *inputfile = (char *) data;

    if (strncmp((char *)inputfile, "desktop", strlen((char *)"desktop")) == 0)
    {
        // capture desktop on X11 Linux
        Display *display = XOpenDisplay(NULL);
        if (display == NULL)
        {
            fprintf(stderr, "Could not find X11 Display.\n");
            return NULL;
        }
        desktop_mode = 1;
        Window root = DefaultRootWindow(display);
        XWindowAttributes attributes;
        XGetWindowAttributes(display, root, &attributes);
        int screen_width = attributes.width;
        int screen_height = attributes.height;
        XCloseDisplay(display);

        fprintf(stderr, "Display Screen: %dx%d\n", screen_width, screen_height);

        if ((screen_width < 16) || (screen_width > 10000))
        {
            screen_width = 640;
        }

        if ((screen_height < 16) || (screen_height > 10000))
        {
            screen_height = 480;
        }

        fprintf(stderr, "Display Screen corrected: %dx%d\n", screen_width, screen_height);
        char *capture_fps = DEFAULT_SCREEN_CAPTURE_FPS;
        if (global_desktop_capture_fps != NULL)
        {
            capture_fps = global_desktop_capture_fps;
        }
        fprintf(stderr, "Display Screen capture FPS: %s\n", capture_fps);

        AVDictionary* options = NULL;
        // set some options
        // grabbing frame rate
        av_dict_set(&options, "framerate", capture_fps, 0);
        // make the grabbed area follow the mouse
        // av_dict_set(&options, "follow_mouse", "centered", 0);

        const int resolution_string_len = 1000;
        char resolution_string[resolution_string_len];
        memset(resolution_string, 0, resolution_string_len);
        snprintf(resolution_string, resolution_string_len, "%dx%d", screen_width, screen_height);
        fprintf(stderr, "Display resolution_string: %s\n", resolution_string);
        av_dict_set(&options, "video_size", resolution_string, 0);
        av_dict_set(&options, "probesize", "50M", 0);

        AVInputFormat *ifmt = av_find_input_format("x11grab");

        // example: grab at position 10,20 ":0.0+10,20"
        if (avformat_open_input(&format_ctx, ":0.0+0,0", ifmt, &options) != 0)
        {
            fprintf(stderr, "Could not open desktop as video input stream.\n");
            return NULL;
        }
    }
    else
    {
        if (strncmp((char *)inputfile, "http://", strlen((char *)"http://")) == 0)
        {
            http_mode = 1;
        }
        else if (strncmp((char *)inputfile, "https://", strlen((char *)"https://")) == 0)
        {
            http_mode = 1;
        }

        // Open the input file
        if ((ret = avformat_open_input(&format_ctx, inputfile, NULL, NULL)) < 0) {
            fprintf(stderr, "Could not open input file '%s'\n", inputfile);
            return NULL; // ret;
        }
    }

    fprintf(stderr, "http_mode=%d\n", http_mode);

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

        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0)
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

            print_codec_parameters_video(codec_params, "VIDEO: ");

            time_base_video = format_ctx->streams[i]->time_base;
        }
    }

    // Make sure we found streams
    if (video_stream_index < 0) {
        fprintf(stderr, "Could not find video streams\n");
        return NULL; // AVERROR_EXIT;
    }

    // Allocate a frame for decoding
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        return NULL; // AVERROR(ENOMEM);
    }

    if ((video_codec_ctx->width > 1920) || (video_codec_ctx->height > 1080))
    {
        calculateBoundingBox_to_fullhd(video_codec_ctx->width,
                        video_codec_ctx->height,
                        &output_width,
                        &output_height);
    }
    else
    {
        output_width = video_codec_ctx->width;
        output_height = video_codec_ctx->height;
    }

    // Allocate a buffer for the YUV data
    uint8_t *yuv_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
            output_width, output_height, 1));
    if (yuv_buffer == NULL) {
        fprintf(stderr, "Error: could not allocate YUV buffer\n");
        return NULL; // 1;
    }

    uint8_t *dst_yuv_buffer[3];
    dst_yuv_buffer[0] = yuv_buffer;
    dst_yuv_buffer[1] = yuv_buffer + (output_width * output_height);
    dst_yuv_buffer[2] = dst_yuv_buffer[1] + ((output_width * output_height) / 4);

    // Create a scaler context to convert the video to YUV
    struct SwsContext *scaler_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height,
            video_codec_ctx->pix_fmt, output_width, output_height,
            AV_PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL);

    if (scaler_ctx == NULL) {
        fprintf(stderr, "Error: could not create scaler context\n");
        return NULL; // 1;
    }

    fprintf(stderr, "SwsContext: %dx%d -> %dx%d\n", video_codec_ctx->width, video_codec_ctx->height, output_width, output_height);

    // Wait for friend to come online
    while ((friend_online == 0) && (main_loop_running))
    {
        yieldcpu(200);
    }

    while ((friend_in_call != 1) && (main_loop_running))
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

    if (format_ctx->streams[video_stream_index]->start_time > 0)
    {
        video_start_time = pts_to_ms(format_ctx->streams[video_stream_index]->start_time, time_base_video);
    }
    printf("stream start time: %ld\n", video_start_time);

    if (format_ctx->streams[video_stream_index]->duration > 0)
    {
        video_duration = pts_to_ms(format_ctx->streams[video_stream_index]->duration, time_base_video);
    }

    if (format_ctx->duration > 0)
    {
        int64_t frame_rate = 0;
        video_duration = format_ctx->duration;
        video_time_base_den = format_ctx->streams[video_stream_index]->time_base.den;
        // int64_t frame_rate = format_ctx->streams[video_stream_index]->avg_frame_rate.num /
        //                 format_ctx->streams[video_stream_index]->avg_frame_rate.den;
        video_length = video_duration / AV_TIME_BASE * 1000;
    }
    printf("stream duration: %ld length: %ld\n", video_duration, video_length);

    while ((ffmpeg_thread_video_stop != 1) && (main_loop_running))
    {
        video_av_starttime = av_gettime();
        // Read packets from the input file and decode them        
        while ((ffmpeg_thread_video_stop != 1) && (friend_online != 0) && (friend_in_call == 1))
        {
            if (av_read_frame(format_ctx, &packet) >= 0)
            {
                if (desktop_mode == 1)
                {
                    ms_desktop_pin = global_pts;
                }
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
                        while (global_play_status == PLAY_PAUSED)
                        {
                            yieldcpu(4);
                        }

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


                        // --------------------
                        // --------------------
                        // ---- Background ----
                        // --------------------
                        // --------------------

                        AVFrame* frame2 = av_frame_clone(frame);
                        av_frame_unref(frame);


                        if (desktop_mode == 1)
                        {
                            struct vsend_data *vs = malloc(sizeof(struct vsend_data));
                            vs->output_width = output_width;
                            vs->output_height = output_height;
                            vs->scaler_ctx = scaler_ctx;
                            vs->frame2 = frame2;
                            vs->video_codec_ctx = video_codec_ctx;
                            vs->format_ctx = format_ctx;
                            vs->dst_yuv_buffer[0] = dst_yuv_buffer[0];
                            vs->dst_yuv_buffer[1] = dst_yuv_buffer[1];
                            vs->dst_yuv_buffer[2] = dst_yuv_buffer[2];
                            vs->video_start_time = video_start_time;
                            vs->desktop_mode = desktop_mode;
                            vs->http_mode = http_mode;
                            vs->video_length = video_length;
                            vs->video_position_percent = video_position_percent;
                            vs->video_stream_index = video_stream_index;
                            vs->ms_desktop_pin = ms_desktop_pin;

                            bool vsend_thread_spawn = 1;
                            pthread_t thread_v_send_bg;
                            pthread_mutex_lock(&vsend___mutex);
                            if (vsend_thread_count > 3)
                            {
                                vsend_thread_spawn = 0;
                            }
                            pthread_mutex_unlock(&vsend___mutex);
                            if (vsend_thread_spawn == 1)
                            {
                                if (pthread_create(&thread_v_send_bg, NULL, thread_v_send_bg_func, (void *)vs) != 0)
                                {
                                    printf("VSend Thread create failed\n");
                                }
                                else
                                {
                                    pthread_mutex_lock(&vsend___mutex);
                                    vsend_thread_count++;
                                    // printf("vsend_thread_count:%d\n", vsend_thread_count);
                                    pthread_mutex_unlock(&vsend___mutex);
                                    pthread_setname_np(thread_v_send_bg, "t_vsend");
                                    if (pthread_detach(thread_v_send_bg))
                                    {
                                        printf("error detaching VSend Thread\n");
                                    }
                                    // pthread_join(thread_v_send_bg, NULL);
                                }
                            }
                        }


#if 1
                        if (desktop_mode != 1)
                        {

                            // Convert the video frame to YUV
                            int planes_stride[3];
                            planes_stride[0] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 0);
                            planes_stride[1] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 1);
                            planes_stride[2] = av_image_get_linesize(AV_PIX_FMT_YUV420P, output_width, 2);
                            // fprintf(stderr, "VideoFrame:strides:%d %d %d\n",planes_stride[0],planes_stride[1],planes_stride[2]);

                            sws_scale(scaler_ctx, (const uint8_t * const*)frame2->data, frame2->linesize, 0, video_codec_ctx->height,
                                    dst_yuv_buffer, planes_stride);

                            int64_t pts = frame2->pts;
                            int64_t ms = pts_to_ms(pts, time_base_video); // convert PTS to milliseconds
                            if (labs(video_start_time) > 2000)
                            {
                                ms = ms - video_start_time;
                            }
                            //**// printf("PTS: %ld / %ld, Time Base: %d/%d, Milliseconds: %ld\n", global_pts, pts, time_base_video.num, time_base_video.den, ms);
                            // printf("TS: frame %ld %ld\n", global_pts, ms);

                            if ((desktop_mode == 1) || (ms > (int64_t)1000*(int64_t)1000*(int64_t)1000*(int64_t)1000))
                            {
                                ms = global_pts;
                                // printf("timestamps broken, just play\n");
                            }
                            else if (http_mode == 1)
                            {
                                ms = global_pts;
                            }
                            else
                            {
                                if (video_length > 0)
                                {
                                    // int64_t current_time = av_gettime() - video_av_starttime;
                                    // int64_t video_cur_position_ms = current_time * video_time_base_den / AV_TIME_BASE;
                                    const int percent_new = (int)calculate_percentage(ms, video_length);
                                    if (percent_new != video_position_percent)
                                    {
                                        video_position_percent = percent_new;
                                        draw_percent_bar(video_position_percent, false);
                                    }
#if 0
                                    printf("curpos:%ld / %ld %d\n",
                                            ms, video_length,
                                            video_position_percent);
                                    int64_t milliseconds = ms % 1000;
                                    int64_t seconds = (ms / 1000) % 60;
                                    int64_t minutes = (ms / (1000 * 60)) % 60;
                                    int64_t hours = (ms / (1000 * 60 * 60)) % 24;
                                    char time_string[20];
                                    snprintf(time_string, sizeof(time_string), "%02ld:%02ld:%02ld.%03ld", hours, minutes, seconds, milliseconds);
                                    printf("Current position: %s\n", time_string);
#endif
                                }
                            }

                            if (global_need_video_seek != 0)
                            {
                                global_need_video_seek = 0;
                                av_frame_unref(frame2);
                                int seek_res = seek_stream(format_ctx, video_codec_ctx, video_stream_index);
                                fprintf(stderr, "seek frame res:%d\n", seek_res);
                                show_seek_forward();
                            }
                            else
                            {
                                bool cond = (global_play_status == PLAY_PLAYING) && ((ms + 600) < global_pts);
                                if (cond)
                                {
                                    // skip frames, we are seeking forward most likely
                                    av_frame_unref(frame2);
                                    //int seek_res = seek_stream(format_ctx, video_codec_ctx, video_stream_index);
                                    // fprintf(stderr, "SKIP frame %d %ld %ld\n", global_play_status, ms, global_pts);
                                    show_seek_forward();
                                }
                                else
                                {
                                    int counter = 0;
                                    const int sleep_ms = 4;
                                    const int one_sec_ms = 1000;

                                    int delay_add = 0;
                                    if (desktop_mode == 0)
                                    {
                                        // video delay only works on real files
                                        delay_add = global_video_delay_factor * 50;
                                    }


                                    while ((global_play_status == PLAY_PAUSED) || ((ms + delay_add) > global_pts))
                                    {
                                        usleep(1000 * sleep_ms);
                                        counter++;
                                        if (counter > (one_sec_ms / sleep_ms))
                                        {
                                            // sleep for max. 1 second
                                            break;
                                        }
                                    }

                                    if (toxav != NULL)
                                    {
                                        if (global_play_status == PLAY_PLAYING)
                                        {
                                            // fprintf(stderr, "frame h:%d %d\n", frame->height, output_height);
                                            uint32_t frame_age_ms = 0;
                                            if (desktop_mode == 1)
                                            {
                                                frame_age_ms = global_pts - ms_desktop_pin;
                                                if (frame_age_ms < 0)
                                                {
                                                    frame_age_ms = 0;
                                                }
                                                else if (frame_age_ms > 1000)
                                                {
                                                    frame_age_ms = 1000;
                                                }
                                                // fprintf(stderr, "frame age:%d\n", frame_age_ms);
                                            }
                                            TOXAV_ERR_SEND_FRAME error2;
                                            bool ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                                                        planes_stride[0], output_height,
                                                        dst_yuv_buffer[0], dst_yuv_buffer[1], dst_yuv_buffer[2],
                                                        &error2, frame_age_ms);

                                            if (error2 != TOXAV_ERR_SEND_FRAME_OK)
                                            {
                                                fprintf(stderr, "toxav_video_send_frame_age:%d %d -> retrying ...\n", (int)ret2, error2);
                                                yieldcpu(1);
                                                frame_age_ms = 1;
                                                ret2 = toxav_video_send_frame_age(toxav, global_friend_num,
                                                            planes_stride[0], output_height,
                                                            dst_yuv_buffer[0], dst_yuv_buffer[1], dst_yuv_buffer[2],
                                                            &error2, frame_age_ms);
                                                if (error2 != TOXAV_ERR_SEND_FRAME_OK)
                                                {
                                                    fprintf(stderr, "toxav_video_send_frame_age:%d %d -> retrying -> FAILED\n", (int)ret2, error2);
                                                }
                                            }
                                        }
                                        else
                                        {
                                            // global_play_status == PLAY_PAUSED
                                        }
                                    }
                                    av_frame_unref(frame2);
                                }
                            }

                        }
#endif

                        // --------------------
                        // --------------------
                        // ---- Background ----
                        // --------------------
                        // --------------------




                    }
                }
                av_packet_unref(&packet);
            }
            else
            {
                fprintf(stderr, "Should not get here\n");
            }
        }

        if (ffmpeg_thread_video_stop != 1)
        {
            // fprintf(stderr, "waiting for friend ...\n");
            yieldcpu(400);
        }
    }

    // Clean up
    av_frame_free(&frame);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    av_free(yuv_buffer);
    sws_freeContext(scaler_ctx);

    printf("ffmpeg Video Thread:Clean thread exit!\n");
    return NULL;
}

static void *ffmpeg_thread_audio_func(void *data)
{
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    AVCodec *audio_codec = NULL;
    AVPacket packet;
    AVFrame *frame = NULL;
    int audio_stream_index = -1;
    int num_samples;
    uint8_t **converted_samples = NULL;
    int ret;
    int desktop_mode = 0;
    const int out_channels = 2; // keep in sync with `out_channel_layout`
    const int out_channel_layout = AV_CH_LAYOUT_STEREO; // AV_CH_LAYOUT_MONO or AV_CH_LAYOUT_STEREO;
    const int out_bytes_per_sample = 2; // 2 byte per PCM16 sample
    const int out_samples = 60 * 48; // X ms @ 48000Hz
    const int out_sample_rate = 48000; // fixed at 48000Hz
    const int temp_audio_buf_sizes = 600000; // fixed buffer
    int audio_delay_in_bytes = 0;
    fifo_buffer_t* audio_pcm_buffer = fifo_buffer_create(temp_audio_buf_sizes);

    char *inputfile = (char *)data;

    if (strncmp((char *)inputfile, "desktop", strlen((char *)"desktop")) == 0)
    {
        desktop_mode = 1;
        AVDictionary* options = NULL;


#ifdef WINDOWS__XXXXXX
        deviceName = DS_GetDefaultDevice("a");
        if (deviceName == "") {
            fprintf(stderr, "AA:Fail to get default audio device, maybe no microphone\n");
        }
        pulse_device_name = "audio=" + deviceName;
        AVInputFormat *inputFormat = av_find_input_format("dshow");
#elif MACOS__XXXXXX
        pulse_device_name = ":0";
        AVInputFormat *inputFormat = av_find_input_format("avfoundation");
        //"[[VIDEO]:[AUDIO]]"
#endif

        // av_dict_set(&options, "framerate", "30", 0);
        AVInputFormat *ifmt = av_find_input_format("pulse");

        char *pulse_device_name = DEFAULT_SCREEN_CAPTURE_PULSE_DEVICE;
        if (global_pulse_inputdevice_name != NULL)
        {
            pulse_device_name = global_pulse_inputdevice_name;
        }

        fprintf(stderr, "AA:using pluse audio device: %s\n", pulse_device_name);

        // "alsa_output.pci-0000_0a_00.3.iec958-stereo.monitor"
        if (avformat_open_input(&format_ctx, pulse_device_name, ifmt, &options) != 0)
        {
            fprintf(stderr, "AA:Could not open pluse audio device as audio input stream.\n");
            return NULL;
        }
    }
    else
    {
        // Open the input file
        if ((ret = avformat_open_input(&format_ctx, inputfile, NULL, NULL)) < 0) {
            fprintf(stderr, "AA:Could not open input file '%s'\n", inputfile);
            return NULL; // ret;
        }
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

            print_codec_parameters_audio(codec_params, "AUDIO: ");

            time_base_audio = format_ctx->streams[i]->time_base;
        }
    }

    // Make sure we found streams
    if (audio_stream_index < 0) {
        fprintf(stderr, "AA:Could not find audio streams\n");
        return NULL; // AVERROR_EXIT;
    }

    // Allocate a frame for decoding
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "AA:Could not allocate frame\n");
        return NULL; // AVERROR(ENOMEM);
    }

    if (strncmp((char *)inputfile, "desktop", strlen((char *)"desktop")) == 0)
    {
        fprintf(stderr, "AA:audio_codec_ctx->channel_layout: %ld AV_CH_LAYOUT_STEREO: %lld default: %ld\n",
            audio_codec_ctx->channel_layout, (long long)AV_CH_LAYOUT_STEREO,
            av_get_default_channel_layout(audio_codec_ctx->channel_layout));
        if (audio_codec_ctx->channel_layout == 0)
        {
            // HINT: no idea what to do here. just guess STEREO?
            audio_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
        }

        fprintf(stderr, "AA:audio_codec_ctx->frame_size: %d\n",
            format_ctx->streams[audio_stream_index]->codecpar->frame_size);
        if (format_ctx->streams[audio_stream_index]->codecpar->frame_size == 0)
        {
            // HINT: no idea what to do here. just guess?
        }
    }

    fprintf(stderr, "AA:audio_codec_ctx->sample_rate: %d\n", format_ctx->streams[audio_stream_index]->codecpar->sample_rate);

    swr_ctx = swr_alloc_set_opts(NULL,
                                 out_channel_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                                 audio_codec_ctx->channel_layout,
                                 format_ctx->streams[audio_stream_index]->codecpar->format,
                                 format_ctx->streams[audio_stream_index]->codecpar->sample_rate,
                                 0, NULL);
    if (!swr_ctx) {
        fprintf(stderr, "AA:Could not allocate resampler context\n");
        fprintf(stderr, "AA:%d %d %d %ld %d %d\n", out_channel_layout,
                AV_SAMPLE_FMT_S16, out_sample_rate, audio_codec_ctx->channel_layout,
                audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate);
        return NULL; // 1;
    }

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "AA:Could not initialize resampler context\n");
        fprintf(stderr, "AA:%d %d %d %ld %d %d\n", out_channel_layout,
                AV_SAMPLE_FMT_S16, out_sample_rate, audio_codec_ctx->channel_layout,
                audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate);
        return NULL; // 1;
    }

    fprintf(stderr, "AA:Audio Config:%d %d %d %ld %d %d\n", out_channel_layout,
            AV_SAMPLE_FMT_S16, out_sample_rate, audio_codec_ctx->channel_layout,
            audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate);


    // Wait for friend to come online
    while ((friend_online == 0) && (main_loop_running))
    {
        yieldcpu(200);
    }

    while ((friend_in_call != 1) && (main_loop_running))
    {
        yieldcpu(200);
    }

    show_novideo_text(0);

    pthread_mutex_lock(&time___mutex);
    if (global_play_status == PLAY_PAUSED)
    {
        global_play_status = PLAY_PLAYING;
        fprintf(stderr, "AA:start playing ...\n");
    }
    pthread_mutex_unlock(&time___mutex);

    uint8_t *buf = (uint8_t *)calloc(1, temp_audio_buf_sizes);

    if (format_ctx->streams[audio_stream_index]->start_time > 0)
    {
        audio_start_time = pts_to_ms(format_ctx->streams[audio_stream_index]->start_time, time_base_audio);
    }
    fprintf(stderr, "AA:stream start time: %ld\n", audio_start_time);

    while ((ffmpeg_thread_audio_stop != 1) && (main_loop_running))
    {
        // Read packets from the input file and decode them        
        while ((av_read_frame(format_ctx, &packet) >= 0) && (ffmpeg_thread_audio_stop != 1)
                && (friend_online != 0) && (friend_in_call == 1))
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
                    while (global_play_status == PLAY_PAUSED)
                    {
                        yieldcpu(4);
                    }

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

                    num_samples = av_rescale_rnd(frame->nb_samples,
                                                        out_sample_rate,
                                                        audio_codec_ctx->sample_rate,
                                                        AV_ROUND_UP);

                    // num_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                    av_samples_alloc_array_and_samples(&converted_samples,
                                NULL,
                                out_channels,
                                num_samples,
                                AV_SAMPLE_FMT_S16,
                                0);
                    int samples_out = swr_convert(swr_ctx,
                            converted_samples,
                            num_samples,
                            (const uint8_t **)frame->extended_data,
                            frame->nb_samples);
                    // printf("AA:samples_out: %d\n", samples_out);
                    const int want_write_bytes = (num_samples * out_channels * out_bytes_per_sample);
                    size_t written_bytes = fifo_buffer_write(audio_pcm_buffer, converted_samples[0], want_write_bytes);
                    // printf("AA:written bytes: %ld wanted: %d\n", written_bytes, want_write_bytes);

                    // Do something with the converted samples here
                    int64_t pts = frame->pts;
                    int64_t ms = pts_to_ms(pts, time_base_audio); // convert PTS to milliseconds
                    if (labs(audio_start_time) > 2000)
                    {
                        ms = ms - audio_start_time;
                    }
                    //**// printf("AA:PTS: %ld / %ld, Time Base: %d/%d, Milliseconds: %ld\n", global_pts, pts, time_base_audio.num, time_base_audio.den, ms);

                    if ((desktop_mode == 1) || (ms > (int64_t)1000*(int64_t)1000*(int64_t)1000*(int64_t)1000))
                    {
                        ms = global_pts;
                        // printf("AA:timestamps broken, just play\n");
                    }

                    if (global_need_audio_seek != 0)
                    {
                        global_need_audio_seek = 0;
                        int seek_res = seek_stream(format_ctx, audio_codec_ctx, audio_stream_index);
                        fprintf(stderr, "AA:seek frame res:%d\n", seek_res);
                    }
                    else
                    {
                        if ((global_play_status == PLAY_PLAYING) && ((ms + 100) < global_pts))
                        {
                            // skip frames, we are seeking forward most likely
                            // drain audio fifo buffer
                            fifo_buffer_read(audio_pcm_buffer, buf, 1000 * out_channels * out_bytes_per_sample);
                            fifo_buffer_read(audio_pcm_buffer, buf, 1000 * out_channels * out_bytes_per_sample);
                            fifo_buffer_read(audio_pcm_buffer, buf, 1000 * out_channels * out_bytes_per_sample);
                            fifo_buffer_read(audio_pcm_buffer, buf, 1000 * out_channels * out_bytes_per_sample);
                            // fprintf(stderr, "AA:SKIP frame\n");
                        }
                        else
                        {
                            int counter = 0;
                            const int sleep_ms = 4;
                            const int one_sec_ms = 1000;
                            while ((global_play_status == PLAY_PAUSED) || (ms > global_pts))
                            {
                                usleep(1000 * sleep_ms);
                                counter++;
                                if (counter > (one_sec_ms / sleep_ms))
                                {
                                    // sleep for max. 1 second
                                    break;
                                }
                            }

                            if (toxav != NULL)
                            {
                                if (global_play_status == PLAY_PLAYING)
                                {
                                    audio_delay_in_bytes = (out_samples * out_channels * out_bytes_per_sample) * global_audio_delay_factor; // n x 60 ms delay
                                    if (fifo_buffer_data_available(audio_pcm_buffer) >= (out_samples * out_channels * out_bytes_per_sample) + (audio_delay_in_bytes))
                                    {
                                        // memset(buf, 0, temp_audio_buf_sizes);
                                        size_t read_bytes = fifo_buffer_read(audio_pcm_buffer, buf, out_samples * out_channels * out_bytes_per_sample);
                                        // printf("AA:read_bytes: %ld\n", read_bytes);
                                        Toxav_Err_Send_Frame error3;
                                        toxav_audio_send_frame(toxav, global_friend_num, (const int16_t *)buf, out_samples,
                                                    out_channels, out_sample_rate, &error3);
                                        if (error3 != TOXAV_ERR_SEND_FRAME_OK)
                                        {
                                            fprintf(stderr, "toxav_audio_send_frame:%d samples=%d channels=%d sr=%d -> retrying ...\n",
                                                error3, out_samples, out_channels, out_sample_rate);
                                            toxav_audio_send_frame(toxav, global_friend_num, (const int16_t *)buf, out_samples,
                                                        out_channels, out_sample_rate, &error3);
                                            if (error3 != TOXAV_ERR_SEND_FRAME_OK)
                                            {
                                                fprintf(stderr, "toxav_audio_send_frame:%d samples=%d channels=%d sr=%d -> retrying again ...\n",
                                                    error3, out_samples, out_channels, out_sample_rate);
                                                yieldcpu(1);
                                                toxav_audio_send_frame(toxav, global_friend_num, (const int16_t *)buf, out_samples,
                                                            out_channels, out_sample_rate, &error3);
                                                if (error3 != TOXAV_ERR_SEND_FRAME_OK)
                                                {
                                                    fprintf(stderr, "toxav_audio_send_frame:%d samples=%d channels=%d sr=%d -> retrying -> FAILED\n",
                                                        error3, out_samples, out_channels, out_sample_rate);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
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

    global_time_cur_ms = 0;
    global_time_old_mono_ts = current_time_monotonic_default2();
    int first_start = 1;

    while ((thread_time_stop != 1) && (main_loop_running))
    {
        yieldcpu(4);
        pthread_mutex_lock(&time___mutex);
        if (global_play_status == PLAY_PLAYING)
        {
            if (first_start == 1)
            {
                first_start = 0;
                global_time_cur_ms = 0;
                global_pts = 0;
                global_time_old_mono_ts = current_time_monotonic_default2();
            }
            global_time_cur_ms = global_time_cur_ms + (int64_t)(current_time_monotonic_default2() - global_time_old_mono_ts);
            global_time_old_mono_ts = current_time_monotonic_default2();
            global_pts = global_time_cur_ms;
        }
        else
        {
            global_time_old_mono_ts = current_time_monotonic_default2();
        }
        pthread_mutex_unlock(&time___mutex);
    }

    printf("Key Time:Clean thread exit!\n");
    return NULL;
}

static void *thread_key_func(void *data)
{
    int ch;
    bool show_pause_text = false;

    Tox *local_tox = (Tox *)data;

    while ((thread_key_stop != 1) && (main_loop_running))
    {
        /* Wait for a keypress */
        while ((1 == 1) && (main_loop_running))
        {
            ch = getchar();
            if (ch == ' ')
            {
                break;
            }
            else if (ch == 'f')
            {
                break;
            }
            else if (ch == 'i')
            {
                break;
            }
            else if (ch == 'a')
            {
                break;
            }
            else if (ch == 'v')
            {
                break;
            }
            else if (ch == 'g')
            {
                break;
            }
            else if (ch == 'b')
            {
                break;
            }
            else if (ch == 'c')
            {
                break;
            }
            else if (ch == 'o')
            {
                break;
            }
            else if (ch == 'h')
            {
                break;
            }
        }

        pthread_mutex_lock(&time___mutex);
        if (ch == ' ')
        {
            if (global_play_status == PLAY_PAUSED)
            {
                global_play_status = PLAY_PLAYING;
                flush_video(0);
                flush_video(0);
                flush_video(0);
                flush_video(0);
                flush_video(0);
                flush_video(0);
                flush_video(0);
                printf("KK:----- PLAY  -----\n");
            }
            else if (global_play_status == PLAY_PLAYING)
            {
                global_play_status = PLAY_PAUSED;
                printf("KK:----- PAUSE -----\n");
                show_pause_text = true;
            }
        }
        else if (ch == 'f')
        {
            global_time_cur_ms = global_time_cur_ms + seek_delta_ms;
            global_time_old_mono_ts = current_time_monotonic_default2();
            global_need_video_seek = 1;
            global_need_audio_seek = 1;
            printf("KK:-----SEEK >>-----\n");
        }
        else if (ch == 'a')
        {
            if (global_audio_delay_factor <= 12)
            {
                global_audio_delay_factor++;
            }
            else
            {
                global_audio_delay_factor = 0;
            }
            printf("KK:-----AUDIO DELAY: %d ms\n", (60 * global_audio_delay_factor));
        }
        else if (ch == 'v')
        {
            if (global_video_delay_factor <= 10)
            {
                global_video_delay_factor++;
            }
            else
            {
                global_video_delay_factor = 0;
            }
            printf("KK:-----VIDEO DELAY: %d ms\n", (50 * global_video_delay_factor));
        }
        else if (ch == 'g')
        {
            global_time_cur_ms = global_time_cur_ms + seek_delta_ms_faster;
            global_time_old_mono_ts = current_time_monotonic_default2();
            global_need_video_seek = 1;
            global_need_audio_seek = 1;
            printf("KK:-----SEEK2>>-----\n");
        }
        else if (ch == 'b')
        {
            global_time_cur_ms = global_time_cur_ms - seek_delta_ms;
            global_time_old_mono_ts = current_time_monotonic_default2();
            global_need_video_seek = 2;
            global_need_audio_seek = 2;
            printf("KK:-----<< SEEK-----\n");
        }
        else if (ch == 'c')
        {
            printf("KK:----- CALL  -----\n");
            toxav_call(toxav, 0, DEFAULT_GLOBAL_AUD_BITRATE, GLOBAL_VID_BITRATE_var, NULL);
        }
        else if (ch == 'o')
        {
            printf("KK:-----  OSD  -----\n");
            if (global_osd_message_toggle == 0)
            {
                const char *message_001 = ".osd 2";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:OSD:send_message:ON:res=%d\n", res_m);
                global_osd_message_toggle = 1;
            }
            else
            {
                const char *message_001 = ".osd 0";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:OSD:send_message:OFF:res=%d\n", res_m);
                global_osd_message_toggle = 0;
            }
        }
        else if (ch == 'i')
        {
            printf("KK:-----HDMIFREQ----\n");
            if (global_hdmifreq_toggle == 60)
            {
                const char *message_001 = ".hdmi 24";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:HDMIFREQ:send_message:24:res=%d\n", res_m);
                global_hdmifreq_toggle = 24;
            }
            else if (global_hdmifreq_toggle == 24)
            {
                const char *message_001 = ".hdmi 25";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:HDMIFREQ:send_message:25:res=%d\n", res_m);
                global_hdmifreq_toggle = 25;
            }
            else if (global_hdmifreq_toggle == 25)
            {
                const char *message_001 = ".hdmi 30";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:HDMIFREQ:send_message:30:res=%d\n", res_m);
                global_hdmifreq_toggle = 30;
            }
            else if (global_hdmifreq_toggle == 30)
            {
                const char *message_001 = ".hdmi 50";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:HDMIFREQ:send_message:50:res=%d\n", res_m);
                global_hdmifreq_toggle = 50;
            }
            else if (global_hdmifreq_toggle == 50)
            {
                const char *message_001 = ".hdmi 60";
                uint32_t res_m = tox_friend_send_message(local_tox, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)message_001, strlen(message_001), NULL);
                printf("KK:HDMIFREQ:send_message:60:res=%d\n", res_m);
                global_hdmifreq_toggle = 60;
            }
        }
        else if (ch == 'h')
        {
            printf("KK:-----HANG UP-----\n");
            toxav_call_control(toxav, 0, TOXAV_CALL_CONTROL_CANCEL, NULL);
            friend_in_call = 0;
        }
        pthread_mutex_unlock(&time___mutex);

        if (show_pause_text)
        {
            show_pause_text = false;
            if (global_play_status == PLAY_PAUSED)
            {
                yieldcpu(1);
                show_pause(850);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(750);
                show_pause(550);
                show_pause(650);
                show_pause(450);
                show_pause(350);
                show_pause(250);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(150);
                show_pause(50);
                show_pause(0);
            }
        }
    }

    printf("Key Thread:Clean thread exit!\n");
    return NULL;
}

// signal handlers --------------------------------------------------
/**
 * @brief Signal handler for INT signal
 *
 * This function is called when the program receives an INT signal.
 * It destroys the scroll area, restores the terminal settings,
 * and sets the main loop running flag to false.
 *
 * @param sig The signal number
 */
void INThandler(int sig)
{
    if (show_progress_bar)
    {
        __shell_percentage__destroy_scroll_area();
    }
    restore_term();
    signal(sig, SIG_IGN);
    printf("_\n");
    printf("INT signal\n");
    main_loop_running = false;
}
// signal handlers --------------------------------------------------

int main(int argc, char *argv[])
{
    main_loop_running = true;

    char *input_file_arg_str = NULL;
    int opt;
    const char     *short_opt = "bhvTti:p:f:xrm:";
    struct option   long_opt[] =
    {
        {"help",          no_argument,       NULL, 'h'},
        {"version",       no_argument,       NULL, 'v'},
        {NULL,            0,                 NULL,  0 }
    };

    while ((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)
    {
        switch (opt)
        {
            case -1:       /* no more arguments */
            case 0:        /* long options toggles */
                break;

            case 't':
                switch_tcponly = 1;
                break;

            case 'T':
                use_tor = 1;
                break;

            case 'b':
                show_progress_bar = true;
                break;

            case 'r':
                vbr = 1;
                GLOBAL_VID_BITRATE_var = 200;
                fprintf(stderr, "using VBR starting at: %d\n", GLOBAL_VID_BITRATE_var);
                break;

            case 'm':
                max_video_bitrate = atoi(optarg);
                if ((max_video_bitrate < 100) || (max_video_bitrate > 8000))
                {
                    max_video_bitrate = 8000;
                }
                fprintf(stderr, "max video bitrate: %d\n", max_video_bitrate);
                break;

            case 'v':
                printf("ToxVideoplayer version: %s\n", global_tox_vplayer_version_string);
                return (0);

            case 'h':
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("  -t,                                  tcp only mode\n");
                printf("  -T,                                  use tor proxy\n");
                printf("  -i,                                  input filename or \"desktop\" to screenshare\n");
                printf("  -r,                                  automatically adjust video bitrate\n");
                printf("  -m,                                  set max. video bitrate\n");
                printf("  -b,                                  show progress bar\n");
                printf("  -p,                                  on \"desktop\" use this as pulse input device\n");
                printf("                                           otherwise \"default\" is used\n");
                printf("  -f,                                  on \"desktop\" use this as capture FPS\n");
                printf("                                           otherwise \"30\" is used\n");
                printf("  -v, --version                        show version\n");
                printf("  -h, --help                           print this help and exit\n");
                printf("\n");
                return (0);

            case 'i':
                input_file_arg_str = optarg;
                break;

            case 'f':
                global_desktop_capture_fps = strdup(optarg);
                if (global_desktop_capture_fps == NULL)
                {
                    fprintf(stderr, "Error copying capture fps string\n");
                    return (-4);
                }
                need_free_global_desktop_capture_fps = 1;
                break;

            case 'p':
                global_pulse_inputdevice_name = strdup(optarg);
                if (global_pulse_inputdevice_name == NULL)
                {
                    fprintf(stderr, "Error copying pulse input device string\n");
                    return (-3);
                }
                need_free_global_pulse_inputdevice_name = 1;
                break;

            case ':':
            case '?':
                fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
                return (-2);

            default:
                fprintf(stderr, "%s: invalid option -- %c\n", argv[0], opt);
                fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
                return (-2);
        }
    }

    printf("ToxVideoplayer version: %s\n", global_tox_vplayer_version_string);

    if (input_file_arg_str == NULL)
    {
        fprintf(stderr, "no input file specified\n");
        return 0;
    }

    fprintf(stderr, "input file: %s\n", input_file_arg_str);

#if (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58,9,100))
    av_register_all();
#endif
    avformat_network_init();
    avdevice_register_all();

    if (pthread_mutex_init(&time___mutex, NULL) != 0)
    {
        fprintf(stderr, "Creating time mutex failed\n");
        return 0;
    }

    if (pthread_mutex_init(&vscale___mutex, NULL) != 0)
    {
        fprintf(stderr, "Creating vscale mutex failed\n");
        return 0;
    }

    if (pthread_mutex_init(&vsend___mutex, NULL) != 0)
    {
        fprintf(stderr, "Creating vsend mutex failed\n");
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

    if (switch_tcponly == 0)
    {
        options.udp_enabled = true; // UDP mode
        printf("setting UDP mode\n");
    }
    else
    {
        options.udp_enabled = false; // TCP mode
        printf("setting TCP mode (tcp option)\n");
    }

    if (use_tor == 1)
    {
        options.udp_enabled = false; // TCP mode
        options.local_discovery_enabled = false;
        printf("setting TCP mode (tor option)\n");
    }

    if (use_tor == 1)
    {
        printf("setting Tor Relay mode\n");
        const char *proxy_host = "127.0.0.1";
        printf("setting proxy_host %s\n", proxy_host);
        uint16_t proxy_port = PROXY_PORT_TOR_DEFAULT;
        printf("setting proxy_port %d\n", (int)proxy_port);
        options.proxy_type = TOX_PROXY_TYPE_SOCKS5;
        options.proxy_host = proxy_host;
        options.proxy_port = proxy_port;
    }
    else
    {
        options.proxy_type = TOX_PROXY_TYPE_NONE;
    }

    FILE *f = fopen(savedata_filename, "rb");
    uint8_t *savedata = NULL;

    if (f)
    {
        fseek(f, 0, SEEK_END);
        size_t savedataSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        savedata = calloc(1, savedataSize);
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

    change_term();

    Tox_Err_New error_tox;
#ifndef TOX_HAVE_TOXUTIL
    printf("init Tox\n");
    Tox *tox = tox_new(&options, &error_tox);
#else
    printf("init Tox [TOXUTIL]\n");
    Tox *tox = tox_utils_new(&options, &error_tox);
#endif
    printf("init Tox res:%d\n", error_tox);

    free(savedata);

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

    // dummy node to bootstrap
    tox_bootstrap(tox, "local", 7766, (uint8_t *)"2AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA1", NULL);

    for (int i = 0; nodes1[i].ip; i++)
    {
        uint8_t *key = (uint8_t *)calloc(1, 100);
        hex_string_to_bin2(nodes1[i].key, key);
        if (!key)
        {
            continue;
        }

        if (use_tor == 0)
        {
            tox_bootstrap(tox, nodes1[i].ip, nodes1[i].udp_port, key, NULL);
        }

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


    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = INThandler;
    sa.sa_flags = 0;// not SA_RESTART!;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (show_progress_bar)
    {
        __shell_percentage__setup_scroll_area();
    }

    draw_percent_bar(1, true);

    tox_iterate(tox, NULL);
    draw_percent_bar(4, true);
    toxav_iterate(toxav);
    draw_percent_bar(6, true);
    // ----------- wait for Tox to come online -----------
    while (main_loop_running)
    {
        tox_iterate(tox, NULL);
        yieldcpu(tox_iteration_interval(tox));
        if (self_online > 0)
        {
            draw_percent_bar(10, true);
            break;
        }
    }
    printf("Tox online\n");
    // ----------- wait for Tox to come online -----------

    ffmpeg_thread_video_stop = 0;
    if (pthread_create(&ffmpeg_thread_video, NULL, ffmpeg_thread_video_func, (void *)input_file_arg_str) != 0)
    {
        printf("ffmpeg Video Thread create failed\n");
    }
    else
    {
        pthread_setname_np(ffmpeg_thread_video, "t_ffmpeg_v");
        printf("ffmpeg Video Thread successfully created\n");
    }


    ffmpeg_thread_audio_stop = 0;
    if (pthread_create(&ffmpeg_thread_audio, NULL, ffmpeg_thread_audio_func, (void *)input_file_arg_str) != 0)
    {
        printf("ffmpeg Audio Thread create failed\n");
    }
    else
    {
        pthread_setname_np(ffmpeg_thread_audio, "t_ffmpeg_a");
        printf("ffmpeg Audio Thread successfully created\n");
    }

    thread_time_stop = 0;
    if (pthread_create(&thread_time, NULL, thread_time_func, (void *)NULL) != 0)
    {
        printf("Time Thread create failed\n");
    }
    else
    {
        pthread_setname_np(thread_time, "t_time");
        printf("Time Thread successfully created\n");
    }

    thread_key_stop = 0;
    if (pthread_create(&thread_key, NULL, thread_key_func, (void *)tox) != 0)
    {
        printf("Key Thread create failed\n");
    }
    else
    {
        pthread_setname_np(thread_key, "t_key");
        printf("Key Thread successfully created\n");
    }


    // ----------- main loop -----------
    while (main_loop_running)
    {
        toxav_iterate(toxav);
        yieldcpu(2);
        tox_iterate(tox, NULL);
        yieldcpu(8);
    }
    // ----------- main loop -----------

    // Clean up
    thread_key_stop = 1;
    //setvbuf(stdin, NULL, _IONBF, 0);
    //fprintf(stdin, "\n");
    //fprintf(stdin, "%c", EOF);
    //fclose(stdin);
    //**// pthread_join(thread_key, NULL);

    ffmpeg_thread_audio_stop = 1;
    pthread_join(ffmpeg_thread_audio, NULL);

    ffmpeg_thread_video_stop = 1;
    pthread_join(ffmpeg_thread_video, NULL);

    thread_time_stop = 1;
    pthread_join(thread_time, NULL);

    pthread_mutex_destroy(&time___mutex);
    pthread_mutex_destroy(&vscale___mutex);
    pthread_mutex_destroy(&vsend___mutex);

    toxav_kill(toxav);
    printf("killed ToxAV\n");
#ifndef TOX_HAVE_TOXUTIL
    tox_kill(tox);
    printf("killed Tox\n");
#else
    tox_utils_kill(tox);
    printf("killed Tox [TOXUTIL]\n");
#endif

    if (need_free_global_desktop_capture_fps == 1)
    {
        free(global_desktop_capture_fps);
    }

    if (need_free_global_pulse_inputdevice_name == 1)
    {
        free(global_pulse_inputdevice_name);
    }

    restore_term();
    if (show_progress_bar)
    {
        __shell_percentage__destroy_scroll_area();
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n");
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("--END--\n");

    return 0;
}

#pragma GCC diagnostic pop

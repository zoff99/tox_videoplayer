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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>

int main(int argc, char *argv[]) {
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
    int got_frame;
    int num_samples;
    uint8_t **converted_samples = NULL;
    int ret;

    if (argc < 2)
    {
        fprintf(stderr, "no input file specified\n");
        return 0;
    }

    fprintf(stderr, "input file: %s\n", argv[1]);

    av_register_all();

    // Open the input file
    if ((ret = avformat_open_input(&format_ctx, argv[1], NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", argv[1]);
        return ret;
    }

    // Retrieve stream information
    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return ret;
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
                return AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(audio_codec_ctx, codec_params)) < 0) {
                fprintf(stderr, "Could not copy audio codec parameters to context\n");
                return ret;
            }
            if ((ret = avcodec_open2(audio_codec_ctx, codec, NULL)) < 0) {
                fprintf(stderr, "Could not open audio codec\n");
                return ret;
            }
            audio_stream_index = i;
            audio_codec = codec;
        } else if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0) {
            video_codec_ctx = avcodec_alloc_context3(codec);
            if (!video_codec_ctx) {
                fprintf(stderr, "Could not allocate video codec context\n");
                return AVERROR(ENOMEM);
            }
            if ((ret = avcodec_parameters_to_context(video_codec_ctx, codec_params)) < 0) {
                fprintf(stderr, "Could not copy video codec parameters to context\n");
                return ret;
            }
            if ((ret = avcodec_open2(video_codec_ctx, codec, NULL)) < 0) {
                fprintf(stderr, "Could not open video codec\n");
                return ret;
            }
            video_stream_index = i;
            video_codec = codec;
        }
    }

    // Make sure we found both audio and video streams
    if (audio_stream_index < 0 || video_stream_index < 0) {
        fprintf(stderr, "Could not find audio and video streams\n");
        return AVERROR_EXIT;
    }

    // Allocate a frame for decoding
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        return AVERROR(ENOMEM);
    }



    swr_ctx = swr_alloc_set_opts(NULL,
                                 AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, audio_codec_ctx->sample_rate,
                                 audio_codec_ctx->channel_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
                                 0, NULL);
    if (!swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        return 1;
    }

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not initialize resampler context\n");
        return 1;
    }




    // Allocate a buffer for the YUV data
    uint8_t *yuv_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
            video_codec_ctx->width, video_codec_ctx->height, 1));
    if (yuv_buffer == NULL) {
        fprintf(stderr, "Error: could not allocate YUV buffer\n");
        return 1;
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
        return 1;
    }

    fprintf(stderr, "SwsContext: %dx%d\n", video_codec_ctx->width, video_codec_ctx->height);


    // Read packets from the input file and decode them
    while (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == audio_stream_index) {
            // Decode audio packet
            ret = avcodec_send_packet(audio_codec_ctx, &packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending audio packet for decoding\n");
                break;
            }

            while (ret >= 0) {
               ret = avcodec_receive_frame(audio_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error during audio decoding\n");
                    break;
                }

                num_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                av_samples_alloc_array_and_samples(&converted_samples, NULL, 2, num_samples, AV_SAMPLE_FMT_S16, 0);
                swr_convert(swr_ctx, converted_samples, num_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);

                // Do something with the converted samples here
                int64_t pts = frame->pts;
                fprintf(stderr, "AudioFrame:fn:%10d pts:%10ld sr:%5d ch:%1d samples:%3d\n",
                    audio_codec_ctx->frame_number, pts, frame->sample_rate, 2, num_samples);

                av_freep(&converted_samples[0]);
                av_freep(&converted_samples);
            }
        } else if (packet.stream_index == video_stream_index) {
            // Decode video packet
            ret = avcodec_send_packet(video_codec_ctx, &packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending video packet for decoding\n");
                break;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(video_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
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
                fprintf(stderr, "VideoFrame:ls:%d %dx%d fn:%10d pts:%10ld pn:%10d\n",
                            frame->linesize[0], frame->width, frame->height,
                            video_codec_ctx->frame_number, pts, frame->coded_picture_number);

                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }

    // Clean up
    av_frame_free(&frame);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    av_free(yuv_buffer);
    swr_free(&swr_ctx);
    sws_freeContext(scaler_ctx);

    return 0;
}

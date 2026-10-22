// iOS cannot start the FFmpeg command that kazarov_core's extraction runs for the movies, so on iOS the extraction
// calls kz_movie_mp4 here instead. It writes what the command writes: the MPEG-2 video copied, the PCM audio
// encoded as ALAC, the timestamps of each input started at zero, no metadata and the index at the front.

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

struct Failure {
    std::string message;
};

int Check(int result, const char *what) {
    if (result < 0) {
        char text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, text, sizeof(text));
        throw Failure{std::string(what) + ": " + text};
    }
    return result;
}

template <typename T>
T *Require(T *value, const char *what) {
    if (value == nullptr) {
        throw Failure{std::string(what) + ": out of memory or unsupported"};
    }
    return value;
}

struct Input {
    AVFormatContext *context = nullptr;
    ~Input() { avformat_close_input(&context); }
};

struct Output {
    AVFormatContext *context = nullptr;
    ~Output() {
        if (context != nullptr) {
            if (!(context->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&context->pb);
            }
            avformat_free_context(context);
        }
    }
};

struct Codec {
    AVCodecContext *context = nullptr;
    ~Codec() { avcodec_free_context(&context); }
};

struct Packet {
    AVPacket *packet = Require(av_packet_alloc(), "packet");
    ~Packet() { av_packet_free(&packet); }
};

struct Frame {
    AVFrame *frame = Require(av_frame_alloc(), "frame");
    ~Frame() { av_frame_free(&frame); }
};

struct Resampler {
    SwrContext *context = nullptr;
    ~Resampler() { swr_free(&context); }
};

struct Fifo {
    AVAudioFifo *fifo = nullptr;
    ~Fifo() {
        if (fifo != nullptr) {
            av_audio_fifo_free(fifo);
        }
    }
};

AVFormatContext *Open(Input &input, const char *path) {
    Check(avformat_open_input(&input.context, path, nullptr, nullptr), path);
    Check(avformat_find_stream_info(input.context, nullptr), path);
    return input.context;
}

// Encodes the whole WAV into ALAC packets in the encoder's time base, which goes in time_base. The audio of one movie is a few tens
// of megabytes of PCM at most, so holding its packets lets the writer interleave them with the video by time.
std::vector<AVPacket *> EncodeAudio(const char *path, AVFormatContext *output, AVStream *&stream,
                                    AVRational &time_base) {
    Input             input;
    AVFormatContext  *wav = Open(input, path);
    const int         index = Check(av_find_best_stream(wav, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0), path);
    const AVStream   *source = wav->streams[index];
    const AVCodec    *decoder = Require(avcodec_find_decoder(source->codecpar->codec_id), "PCM decoder");
    Codec             decode;
    decode.context = Require(avcodec_alloc_context3(decoder), "PCM decoder");
    Check(avcodec_parameters_to_context(decode.context, source->codecpar), "PCM decoder");
    Check(avcodec_open2(decode.context, decoder, nullptr), "PCM decoder");
    // A WAV names only its channel count; the encoder and the resampler take the usual layout for it.
    AVChannelLayout layout = {};
    av_channel_layout_default(&layout, decode.context->ch_layout.nb_channels);

    const AVCodec *encoder = Require(avcodec_find_encoder(AV_CODEC_ID_ALAC), "ALAC encoder");
    Codec          encode;
    encode.context = Require(avcodec_alloc_context3(encoder), "ALAC encoder");
    encode.context->sample_rate = decode.context->sample_rate;
    encode.context->sample_fmt = AV_SAMPLE_FMT_S16P;
    encode.context->time_base = {1, decode.context->sample_rate};
    encode.context->flags |= AV_CODEC_FLAG_BITEXACT;
    Check(av_channel_layout_copy(&encode.context->ch_layout, &layout), "ALAC encoder");
    if (output->oformat->flags & AVFMT_GLOBALHEADER) {
        encode.context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    Check(avcodec_open2(encode.context, encoder, nullptr), "ALAC encoder");
    stream = Require(avformat_new_stream(output, nullptr), "audio stream");
    Check(avcodec_parameters_from_context(stream->codecpar, encode.context), "audio stream");
    stream->time_base = encode.context->time_base;
    time_base = encode.context->time_base;

    Resampler resample;
    Check(swr_alloc_set_opts2(&resample.context, &encode.context->ch_layout, encode.context->sample_fmt,
                              encode.context->sample_rate, &layout, decode.context->sample_fmt,
                              decode.context->sample_rate, 0, nullptr),
          "resampler");
    Check(swr_init(resample.context), "resampler");
    Fifo fifo;
    fifo.fifo = Require(av_audio_fifo_alloc(encode.context->sample_fmt, encode.context->ch_layout.nb_channels, 1),
                        "audio FIFO");
    const int frame_size = encode.context->frame_size > 0 ? encode.context->frame_size : 4096;

    std::vector<AVPacket *> packets;
    std::int64_t            next_pts = 0;
    Packet                  encoded;
    auto drain = [&] {
        for (;;) {
            const int result = avcodec_receive_packet(encode.context, encoded.packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            Check(result, "ALAC encoder");
            encoded.packet->stream_index = stream->index;
            packets.push_back(Require(av_packet_clone(encoded.packet), "audio packet"));
            av_packet_unref(encoded.packet);
        }
    };
    auto send = [&](bool flush) {
        while (av_audio_fifo_size(fifo.fifo) >= frame_size || (flush && av_audio_fifo_size(fifo.fifo) > 0)) {
            Frame chunk;
            chunk.frame->nb_samples = std::min(frame_size, av_audio_fifo_size(fifo.fifo));
            chunk.frame->format = encode.context->sample_fmt;
            chunk.frame->sample_rate = encode.context->sample_rate;
            Check(av_channel_layout_copy(&chunk.frame->ch_layout, &encode.context->ch_layout), "audio frame");
            Check(av_frame_get_buffer(chunk.frame, 0), "audio frame");
            Check(av_audio_fifo_read(fifo.fifo, reinterpret_cast<void **>(chunk.frame->data), chunk.frame->nb_samples),
                  "audio FIFO");
            chunk.frame->pts = next_pts;
            next_pts += chunk.frame->nb_samples;
            Check(avcodec_send_frame(encode.context, chunk.frame), "ALAC encoder");
            drain();
        }
    };
    auto convert = [&](const AVFrame *pcm) {
        const int capacity = swr_get_out_samples(resample.context, pcm != nullptr ? pcm->nb_samples : 0);
        if (capacity <= 0) {
            return;
        }
        Frame converted;
        converted.frame->nb_samples = capacity;
        converted.frame->format = encode.context->sample_fmt;
        converted.frame->sample_rate = encode.context->sample_rate;
        Check(av_channel_layout_copy(&converted.frame->ch_layout, &encode.context->ch_layout), "audio frame");
        Check(av_frame_get_buffer(converted.frame, 0), "audio frame");
        const int samples = Check(swr_convert(resample.context, converted.frame->data, capacity,
                                              pcm != nullptr ? const_cast<const std::uint8_t **>(pcm->extended_data)
                                                             : nullptr,
                                              pcm != nullptr ? pcm->nb_samples : 0),
                                  "resampler");
        if (samples > 0) {
            Check(av_audio_fifo_write(fifo.fifo, reinterpret_cast<void **>(converted.frame->data), samples),
                  "audio FIFO");
        }
    };

    Packet read;
    Frame  pcm;
    auto   decoded = [&] {
        for (;;) {
            const int result = avcodec_receive_frame(decode.context, pcm.frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            Check(result, "PCM decoder");
            convert(pcm.frame);
            av_frame_unref(pcm.frame);
            send(false);
        }
    };
    while (av_read_frame(wav, read.packet) >= 0) {
        if (read.packet->stream_index == index) {
            Check(avcodec_send_packet(decode.context, read.packet), "PCM decoder");
            decoded();
        }
        av_packet_unref(read.packet);
    }
    Check(avcodec_send_packet(decode.context, nullptr), "PCM decoder");
    decoded();
    convert(nullptr);
    send(true);
    Check(avcodec_send_frame(encode.context, nullptr), "ALAC encoder");
    drain();
    return packets;
}

void Convert(const char *video_path, const char *audio_path, const char *output_path) {
    Input video_input;
    // The program stream leaves the pts off some pictures; MP4 needs one on every packet, as the command makes.
    video_input.context = Require(avformat_alloc_context(), "video input");
    video_input.context->flags |= AVFMT_FLAG_GENPTS;
    AVFormatContext *video = Open(video_input, video_path);
    const int        video_index = av_find_best_stream(video, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    Check(video_index, video_path);
    const AVStream *video_source = video->streams[video_index];

    Output output;
    Check(avformat_alloc_output_context2(&output.context, nullptr, "mp4", output_path), output_path);
    output.context->flags |= AVFMT_FLAG_BITEXACT;
    AVStream *video_stream = Require(avformat_new_stream(output.context, nullptr), "video stream");
    Check(avcodec_parameters_copy(video_stream->codecpar, video_source->codecpar), "video stream");
    video_stream->codecpar->codec_tag = 0;
    video_stream->time_base = video_source->time_base;

    AVStream               *audio_stream = nullptr;
    AVRational              audio_time_base = {1, 1};
    std::vector<AVPacket *> audio;
    struct FreePackets {
        std::vector<AVPacket *> &packets;
        ~FreePackets() {
            for (AVPacket *&packet : packets) {
                av_packet_free(&packet);
            }
        }
    } free_audio{audio};
    if (audio_path != nullptr) {
        audio = EncodeAudio(audio_path, output.context, audio_stream, audio_time_base);
    }

    Check(avio_open(&output.context->pb, output_path, AVIO_FLAG_WRITE), output_path);
    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    const int header = avformat_write_header(output.context, &options);
    av_dict_free(&options);
    Check(header, output_path);

    // -start_at_zero: the video's earliest time becomes zero; the WAV already starts there.
    const std::int64_t video_offset = video->start_time == AV_NOPTS_VALUE ?
                                          0 :
                                          av_rescale_q(video->start_time, AV_TIME_BASE_Q, video_source->time_base);
    std::size_t next_audio = 0;
    auto        write_audio_before = [&](const AVPacket *video_packet) {
        while (next_audio < audio.size() &&
               (video_packet == nullptr ||
                av_compare_ts(audio[next_audio]->dts, audio_time_base, video_packet->dts,
                              video_stream->time_base) <= 0)) {
            av_packet_rescale_ts(audio[next_audio], audio_time_base, audio_stream->time_base);
            Check(av_interleaved_write_frame(output.context, audio[next_audio]), "audio packet");
            av_packet_free(&audio[next_audio]);
            ++next_audio;
        }
    };
    Packet packet;
    while (av_read_frame(video, packet.packet) >= 0) {
        if (packet.packet->stream_index != video_index) {
            av_packet_unref(packet.packet);
            continue;
        }
        if (packet.packet->pts != AV_NOPTS_VALUE) {
            packet.packet->pts -= video_offset;
        }
        if (packet.packet->dts != AV_NOPTS_VALUE) {
            packet.packet->dts -= video_offset;
        }
        packet.packet->stream_index = video_stream->index;
        av_packet_rescale_ts(packet.packet, video_source->time_base, video_stream->time_base);
        packet.packet->pos = -1;
        if (packet.packet->dts != AV_NOPTS_VALUE) {
            write_audio_before(packet.packet);
        }
        Check(av_interleaved_write_frame(output.context, packet.packet), "video packet");
    }
    write_audio_before(nullptr);
    Check(av_write_trailer(output.context), output_path);
}

} // namespace

extern "C" int kz_movie_mp4(const char *video, const char *audio, const char *output, char *error,
                            std::size_t error_size) {
    try {
        Convert(video, audio, output);
        return 0;
    } catch (const Failure &failure) {
        std::snprintf(error, error_size, "%s", failure.message.c_str());
    } catch (const std::exception &exception) {
        std::snprintf(error, error_size, "%s", exception.what());
    }
    std::remove(output);
    return 1;
}

#endif

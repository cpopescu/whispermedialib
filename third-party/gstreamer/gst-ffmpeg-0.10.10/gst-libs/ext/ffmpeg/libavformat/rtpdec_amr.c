/*
 * RTP AMR Depacketizer, RFC 3267
 * Copyright (c) 2010 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "rtpdec_amr.h"
#include "libavutil/avstring.h"

static const uint8_t frame_sizes_nb[16] = {
    12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t frame_sizes_wb[16] = {
    17, 23, 32, 36, 40, 46, 50, 58, 60, 5, 5, 0, 0, 0, 0, 0
};

static int amr_handle_packet(AVFormatContext *ctx,
                             PayloadContext *data,
                             AVStream *st,
                             AVPacket * pkt,
                             uint32_t * timestamp,
                             const uint8_t * buf,
                             int len, int flags)
{
    const uint8_t *frame_sizes = NULL;
    int frames;
    int i;
    const uint8_t *speech_data;
    uint8_t *ptr;

    if (st->codec->codec_id == CODEC_ID_AMR_NB) {
        frame_sizes = frame_sizes_nb;
    } else if (st->codec->codec_id == CODEC_ID_AMR_WB) {
        frame_sizes = frame_sizes_wb;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Bad codec ID\n");
        return AVERROR_INVALIDDATA;
    }

    if (st->codec->channels != 1) {
        av_log(ctx, AV_LOG_ERROR, "Only mono AMR is supported\n");
        return AVERROR_INVALIDDATA;
    }

    /* The AMR RTP packet consists of one header byte, followed
     * by one TOC byte for each AMR frame in the packet, followed
     * by the speech data for all the AMR frames.
     *
     * The header byte contains only a codec mode request, for
     * requesting what kind of AMR data the sender wants to
     * receive. Not used at the moment.
     */

    /* Count the number of frames in the packet. The highest bit
     * is set in a TOC byte if there are more frames following.
     */
    for (frames = 1; frames < len && (buf[frames] & 0x80); frames++) ;

    if (1 + frames >= len) {
        /* We hit the end of the packet while counting frames. */
        av_log(ctx, AV_LOG_ERROR, "No speech data found\n");
        return AVERROR_INVALIDDATA;
    }

    speech_data = buf + 1 + frames;

    /* Everything except the codec mode request byte should be output. */
    if (av_new_packet(pkt, len - 1)) {
        av_log(ctx, AV_LOG_ERROR, "Out of memory\n");
        return AVERROR_NOMEM;
    }
    pkt->stream_index = st->index;
    ptr = pkt->data;

    for (i = 0; i < frames; i++) {
        uint8_t toc = buf[1 + i];
        int frame_size = frame_sizes[(toc >> 3) & 0x0f];

        if (speech_data + frame_size > buf + len) {
            /* Too little speech data */
            av_log(ctx, AV_LOG_WARNING, "Too little speech data in the RTP packet\n");
            /* Set the unwritten part of the packet to zero. */
            memset(ptr, 0, pkt->data + pkt->size - ptr);
            pkt->size = ptr - pkt->data;
            return 0;
        }

        /* Extract the AMR frame mode from the TOC byte */
        *ptr++ = toc & 0x7C;

        /* Copy the speech data */
        memcpy(ptr, speech_data, frame_size);
        speech_data += frame_size;
        ptr += frame_size;
    }

    if (speech_data < buf + len) {
        av_log(ctx, AV_LOG_WARNING, "Too much speech data in the RTP packet?\n");
        /* Set the unwritten part of the packet to zero. */
        memset(ptr, 0, pkt->data + pkt->size - ptr);
        pkt->size = ptr - pkt->data;
    }

    return 0;
}

static int amr_parse_sdp_line(AVFormatContext *s, int st_index,
                              PayloadContext *data, const char *line)
{
    const char *p;
    char attr[25], value[25];

    /* Parse an fmtp line this one:
     * a=fmtp:97 octet-align=1; interleaving=0
     * That is, a normal fmtp: line followed by semicolon & space
     * separated key/value pairs.
     */
    if (av_strstart(line, "fmtp:", &p)) {
        int octet_align = 0;
        int crc = 0;
        int interleaving = 0;
        int channels = 1;

        while (*p && *p == ' ') p++; /* strip spaces */
        while (*p && *p != ' ') p++; /* eat protocol identifier */
        while (*p && *p == ' ') p++; /* strip trailing spaces */

        while (rtsp_next_attr_and_value(&p, attr, sizeof(attr), value, sizeof(value))) {
            if (!strcmp(attr, "octet-align"))
                octet_align = atoi(value);
            else if (!strcmp(attr, "crc"))
                crc = atoi(value);
            else if (!strcmp(attr, "interleaving"))
                interleaving = atoi(value);
            else if (!strcmp(attr, "channels"))
                channels = atoi(value);
        }
        if (!octet_align || crc || interleaving || channels != 1) {
            av_log(s, AV_LOG_ERROR, "Unsupported RTP/AMR configuration!\n");
            return -1;
        }
    }
    return 0;
}

RTPDynamicProtocolHandler ff_amr_nb_dynamic_handler = {
    .enc_name         = "AMR",
    .codec_type       = CODEC_TYPE_AUDIO,
    .codec_id         = CODEC_ID_AMR_NB,
    .parse_sdp_a_line = amr_parse_sdp_line,
    .parse_packet     = amr_handle_packet,
};

RTPDynamicProtocolHandler ff_amr_wb_dynamic_handler = {
    .enc_name         = "AMR-WB",
    .codec_type       = CODEC_TYPE_AUDIO,
    .codec_id         = CODEC_ID_AMR_WB,
    .parse_sdp_a_line = amr_parse_sdp_line,
    .parse_packet     = amr_handle_packet,
};


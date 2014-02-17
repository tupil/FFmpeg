// include this file at the and of libavformat/mux.c

extern int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset);

int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset)
{
    int ret, flush = 0;

    ret = check_packet(s, pkt);
    if (ret < 0)
        goto fail;

    if (pkt) {
        AVStream *st = s->streams[pkt->stream_index];

        //FIXME/XXX/HACK drop zero sized packets
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO && pkt->size == 0) {
            ret = 0;
            goto fail;
        }

        av_dlog(s, "av_interleaved_write_frame_with_offset size:%d dts:%s pts:%s\n",
                pkt->size, av_ts2str(pkt->dts), av_ts2str(pkt->pts));
        if ((ret = compute_muxer_pkt_fields(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
            goto fail;

        if (pkt->pts != AV_NOPTS_VALUE)
	        pkt->pts += offset;
        if (pkt->dts != AV_NOPTS_VALUE)
	        pkt->dts += offset;

        av_dlog(s, "PTS before, after: %s, %s, %s\n", av_ts2str(pkt->pts-offset), av_ts2str(pkt->pts), av_ts2str(offset));
        av_dlog(s, "DTS before, after: %s, %s, %s\n", av_ts2str(pkt->dts-offset), av_ts2str(pkt->dts), av_ts2str(offset));

        if (pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        av_dlog(s, "av_interleaved_write_frame FLUSH\n");
        flush = 1;
    }

    for (;; ) {
        AVPacket opkt;
        int ret = interleave_packet(s, &opkt, pkt, flush);
        if (pkt) {
            memset(pkt, 0, sizeof(*pkt));
            av_init_packet(pkt);
            pkt = NULL;
        }
        if (ret <= 0) //FIXME cleanup needed for ret<0 ?
            return ret;

        ret = write_packet(s, &opkt);
        if (ret >= 0)
            s->streams[opkt.stream_index]->nb_frames++;

        av_free_packet(&opkt);

        if (ret < 0)
            return ret;
        if(s->pb && s->pb->error)
            return s->pb->error;
    }
fail:
    av_packet_unref(pkt);
    return ret;
}

// include this file at the and of libavformat/mux.c

extern int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset);

int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset)
{
    int ret, flush = 0, i;

    ret = prepare_input_packet(s, pkt);
    if (ret < 0)
        goto fail;

    if (pkt) {
        AVStream *st = s->streams[pkt->stream_index];

        if (s->oformat->check_bitstream) {
            if (!st->internal->bitstream_checked) {
                if ((ret = s->oformat->check_bitstream(s, pkt)) < 0)
                    goto fail;
                else if (ret == 1)
                    st->internal->bitstream_checked = 1;
            }
        }

        for (i = 0; i < st->internal->nb_bsfcs; i++) {
            AVBSFContext *ctx = st->internal->bsfcs[i];
            if (i > 0) {
                AVBSFContext* prev_ctx = st->internal->bsfcs[i - 1];
                if (prev_ctx->par_out->extradata_size != ctx->par_in->extradata_size) {
                    if ((ret = avcodec_parameters_copy(ctx->par_in, prev_ctx->par_out)) < 0)
                        goto fail;
                }
            }
            // TODO: when any bitstream filter requires flushing at EOF, we'll need to
            // flush each stream's BSF chain on write_trailer.
            if ((ret = av_bsf_send_packet(ctx, pkt)) < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Failed to send packet to filter %s for stream %d",
                       ctx->filter->name, pkt->stream_index);
                goto fail;
            }
            // TODO: when any automatically-added bitstream filter is generating multiple
            // output packets for a single input one, we'll need to call this in a loop
            // and write each output packet.
            if ((ret = av_bsf_receive_packet(ctx, pkt)) < 0) {
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return 0;
                av_log(ctx, AV_LOG_ERROR,
                       "Failed to send packet to filter %s for stream %d",
                       ctx->filter->name, pkt->stream_index);
                goto fail;
            }
            if (i == st->internal->nb_bsfcs - 1) {
                if (ctx->par_out->extradata_size != st->codecpar->extradata_size) {
                    if ((ret = avcodec_parameters_copy(st->codecpar, ctx->par_out)) < 0)
                        goto fail;
                }
            }
        }

        if (s->debug & FF_FDEBUG_TS)
            av_log(s, AV_LOG_TRACE, "av_interleaved_write_frame_with_offset size:%d dts:%s pts:%s\n",
                pkt->size, av_ts2str(pkt->dts), av_ts2str(pkt->pts));

#if FF_API_COMPUTE_PKT_FIELDS2 && FF_API_LAVF_AVCTX
        if ((ret = compute_muxer_pkt_fields(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
            goto fail;
#endif

        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;

        if (s->debug & FF_FDEBUG_TS) {
            av_log(s, "PTS before, after: %s, %s, %s\n", av_ts2str(pkt->pts-offset), av_ts2str(pkt->pts), av_ts2str(offset));
            av_log(s, "DTS before, after: %s, %s, %s\n", av_ts2str(pkt->dts-offset), av_ts2str(pkt->dts), av_ts2str(offset));
        }

        if (pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        av_log(s, AV_LOG_TRACE, "av_interleaved_write_frame FLUSH\n");
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

        av_packet_unref(&opkt);

        if (ret < 0)
            return ret;
        if(s->pb && s->pb->error)
            return s->pb->error;
    }
fail:
    av_packet_unref(pkt);
    return ret;
}

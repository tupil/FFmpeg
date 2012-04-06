// include this file at the and of libavformat/utils.c

extern int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset);

int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset)
{
    AVStream *st= s->streams[ pkt->stream_index];

    //FIXME/XXX/HACK drop zero sized packets
    if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO && pkt->size==0)
        return 0;

//av_log(NULL, AV_LOG_DEBUG, "av_interleaved_write_frame %d %"PRId64" %"PRId64"\n", pkt->size, pkt->dts, pkt->pts);
    if(compute_pkt_fields2(s, st, pkt) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return -1;

	if (pkt->pts != AV_NOPTS_VALUE)
	    pkt->pts += offset;
	if (pkt->dts != AV_NOPTS_VALUE)
	    pkt->dts += offset;

//    fprintf(stderr, "DTS before, after: %lld, %lld, %i\n", pkt->dts-offset, pkt->dts, offset);

    if(pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return -1;

    for(;;){
        AVPacket opkt;
        int ret= interleave_packet(s, &opkt, pkt, 0);
        if(ret<=0) //FIXME cleanup needed for ret<0 ?
            return ret;

        ret= s->oformat->write_packet(s, &opkt);

        av_free_packet(&opkt);
        pkt= NULL;

        if(ret<0)
            return ret;
        if(s->pb && s->pb->error)
            return s->pb->error;
    }
}

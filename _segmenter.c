#ifdef WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#endif

static double prev_segment_time = -1;

extern int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset);

static int segment_length = 0;
static int segment_offset = 0;

static int64_t segmenter(AVFormatContext *s, AVPacket *pkt, AVCodecContext *avctx)
{ 
	AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    double segment_time = prev_segment_time;
    uint64_t offset = 0;    
    
    for (int i = 0; i < s->nb_streams; ++i)
    {
    	AVStream *stream = s->streams[i];
    	if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_stream == NULL)
    	{
    		video_stream = stream;
    	}
    	else if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream == NULL)
    	{
    		audio_stream = stream;
    	}
    }
	
	if (segment_length == 0)
	{
		return 0;
	}
	
	if (prev_segment_time == -1)
	{
		prev_segment_time = 0; // segment_offset;
	}
	
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
	    AVStream *video_st = s->streams[pkt->stream_index];    	
    	if (pkt->flags & AV_PKT_FLAG_KEY) 
        {
            segment_time = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
        }
    }
    else if (video_stream == NULL)
    {
		// only do this when there is no video stream, otherwise the segment break will not be on keyframe
    	AVStream *st = s->streams[pkt->stream_index];    	
        segment_time = (double)st->pts.val * st->time_base.num / st->time_base.den;
    }
    
 	if (segment_time - prev_segment_time >= segment_length) 
    {
    	const char *sbreak = "-------SEGMENT-BREAK-------";
		avio_write(s->pb, sbreak, strlen(sbreak));
        avio_flush(s->pb);
						
        prev_segment_time += segment_length;
    }
    
    if (video_stream != NULL)
    {    	
	    offset = segment_offset * video_stream->time_base.den / video_stream->time_base.num;    	
    }
    else if (audio_stream != NULL)
    {
	    offset = segment_offset * audio_stream->time_base.den / audio_stream->time_base.num;    	    	
    }
    return offset;
}

static int segmenter_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt, AVCodecContext *avctx)
{
	float offset = segmenter(s, pkt, avctx);
	return av_interleaved_write_frame_with_offset(s, pkt, offset); 
}

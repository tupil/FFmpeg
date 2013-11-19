static double prev_segment_time = -1;

extern int av_interleaved_write_frame_with_offset(AVFormatContext *s, AVPacket *pkt, uint64_t offset);

static int segment_length = 0;
static int segment_offset = 0;
static int segment_iframe_only = 1;
static int64_t start_pts = AV_NOPTS_VALUE;

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

    if (start_pts == AV_NOPTS_VALUE)
    {
        start_pts = pkt->pts;
    }
	
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
	    AVStream *video_st = s->streams[pkt->stream_index];    	
       if (pkt->flags & AV_PKT_FLAG_KEY || segment_iframe_only == 0)
        {
            segment_time = (double)(pkt->pts - start_pts) * video_st->time_base.num / video_st->time_base.den;
        }
    }
    else if (video_stream == NULL)
    {
		// only do this when there is no video stream, otherwise the segment break will not be on keyframe
    	AVStream *st = s->streams[pkt->stream_index];    	
        segment_time = (double)(pkt->pts - start_pts) * st->time_base.num / st->time_base.den;
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

int64_t seeked_to = 0;

static int av_seek_frame2(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
	seeked_to = timestamp;
	return av_seek_frame(s, stream_index, timestamp, flags);
}

#define av_seek_frame av_seek_frame2

#define fix_resample() ost->video_resample = 1;

#define report_padding() do_report_padding(output_files);

int ffmpeg_main(int argc, char **argv);


static void preprocess_arguments(int *argc, char ***argv)
{
	char **res = av_malloc(*argc * sizeof(char*));
	int index = 0;
	
	for (int i = 0; i < *argc; ++i)
	{
	    if (strcmp((*argv)[i], "--segment-length") == 0)
	    {
	    	sscanf((*argv)[i+1], "%i", &segment_length);
	    	i += 1;
	    	continue;
	    }
	    else if (strcmp((*argv)[i], "--segment-offset") == 0)
	    {
	    	sscanf((*argv)[i+1], "%i", &segment_offset);
	    	i += 1;
	    	continue;
	    }
        else if (strcmp((*argv)[i], "--segment-iframe-only") == 0)
        {
                sscanf((*argv)[i+1], "%i", &segment_iframe_only);
                i += 1;
                continue;
        }
		
		res[index++] = (*argv)[i];
		
	}	
	*argv = res;
	*argc = index;
}


int main(int argc, char **argv)
{
	int res;	
	preprocess_arguments(&argc, &argv);
	res = ffmpeg_main(argc, argv);
	return res;
}

// This method fill the pipe buffer on stderr during regular conversion so that the server can pause ffmpeg process
static void do_report_padding(AVFormatContext **output_files)
{
#define BUFFER_SIZE 4096
	
    if (strcmp(output_files[0]->filename, "pipe:") == 0)
        return;
    
    char buffer[BUFFER_SIZE];
    memset(buffer, ' ', BUFFER_SIZE);
    buffer[BUFFER_SIZE-1]=0;
    fprintf(stderr, "%IGNORE %s\n", buffer);	
}


#define main ffmpeg_main


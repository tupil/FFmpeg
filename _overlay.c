#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "libavcodec/avcodec.h"
//#include "libavcodec/colorspace.h"
#if HAVE_WINSOCK2_H == 1
#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define EWOULDBLOCK WSAEWOULDBLOCK
#define ECONNREFUSED WSAECONNREFUSED
#else
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif   
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "_segmenter.c"

static inline int socket_error()
{
#if HAVE_WINSOCK2_H == 1
	return WSAGetLastError();
#else
	return errno;
#endif
}

static void init_sockets()
{
#if HAVE_WINSOCK2_H == 1
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) 
    {
        fprintf(stderr, "WSAStartup failed with error: %d\n", err);    
    }

#endif
}

typedef struct
{
	const char *conversion_id;
	int width;
	int height;
	int timestamp;
}
OverlayRequest;

typedef struct
{
	int left;
	int top;
	int width;
	int height;
	uint8_t *data;
}
Overlay;

typedef struct
{
	Overlay **overlays;
	int overlay_count;
	int valid_until_timestamp;
}
OverlayResponse;

static OverlayResponse *overlay_response_init()
{
	OverlayResponse *resp = av_mallocz(sizeof(OverlayResponse));
	return resp;
}

static void overlay_response_free(OverlayResponse *resp)
{
	if (resp != NULL)
	{
		if (resp->overlays != NULL)
		{
			for (int i = 0; i < resp->overlay_count; ++i)
			{
				if (resp->overlays[i] != NULL)
				{
					if (resp->overlays[i]->data != NULL)
					{
						av_free(resp->overlays[i]->data);
					}
					
					av_free(resp->overlays[i]);
				}				
			}
			resp->overlays = NULL;
		}
		
		av_free(resp);
	}
}

static int recv_fully(int socket, void *buf, int size)
{
	uint8_t *byte_buffer = buf;
	int remaining = size;
	
	while (remaining > 0)
	{
		int numread = recv(socket, byte_buffer, remaining, 0);
		if (numread <= 0)
		{
			return numread;
		}
		byte_buffer += numread;
		remaining -= numread;
	}
	return size;
}

static int send_fully(int socket, const void *buf, int size)
{
	const uint8_t *byte_buffer = buf;
	int remaining = size;
	
	while (remaining > 0)
	{
		int numsent = send(socket, byte_buffer, remaining, 0);
		if (numsent <= 0)
		{
			return numsent;
		}
		byte_buffer += numsent;
		remaining -= numsent;
	}
	return size;
}

static inline int send_int(int socket, int value)
{
	value = htonl(value);
	return send_fully(socket, &value, sizeof(int));
}

#define __BAIL(condition) if ((condition) < 0) return -1;

static int send_overlay_request(int socket, OverlayRequest *req)
{
	if (req->conversion_id == NULL)
	{
		__BAIL(send_int(socket, 0));		
	}
	else 
	{
		int len = strlen(req->conversion_id);
		__BAIL(send_int(socket, len));
		if (len > 0)
		{
			__BAIL(send_fully(socket, req->conversion_id, len));
		}
	}
	__BAIL(send_int(socket, req->width));
	__BAIL(send_int(socket, req->height));
	__BAIL(send_int(socket, req->timestamp));
	return 0;
}

static inline int read_int(int socket, int *value)
{
	int i;
	int res = recv_fully(socket, &i, sizeof(int));
	*value = ntohl(i);
	return res;
}

static int read_overlay_response(int socket, OverlayResponse *resp)
{
	__BAIL(read_int(socket, &resp->valid_until_timestamp));
	__BAIL(read_int(socket, &resp->overlay_count));
	
	if (resp->overlay_count == 0)
	{
		resp->overlays = NULL;
	}
	else 
	{
		int size;
		resp->overlays = av_mallocz(resp->overlay_count * sizeof(Overlay));
		
		for (int i = 0; i < resp->overlay_count; ++i)
		{
			Overlay *overlay = av_mallocz(sizeof(Overlay));
			resp->overlays[i] = overlay;
			__BAIL(read_int(socket, &overlay->left));
			__BAIL(read_int(socket, &overlay->top));
			__BAIL(read_int(socket, &overlay->width));
			__BAIL(read_int(socket, &overlay->height));
			
			size = overlay->width * overlay->height * 4;			
			
			if (size == 0)
			{
				overlay->data = NULL;
			}
			else 
			{
				overlay->data = av_mallocz(size);
				__BAIL(recv_fully(socket, overlay->data, size));
			}
		}
		
	}

	return 0;
}

static int overlay_port_number = -1;
static int overlay_sock = -1;
static const char *overlay_conversion_id;

static int get_socket()
{
	if (overlay_sock != -1)
	{
		return overlay_sock;
	}
	else if (overlay_port_number != -1)
	{
		struct hostent *host;
		struct sockaddr_in server_addr;  
		
		host = gethostbyname("127.0.0.1");
		
		if ((overlay_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
		{
			fprintf(stderr, "Error creating socket: %i\n", socket_error());
			overlay_port_number = -1;
			return -1;
		}
		
		server_addr.sin_family = AF_INET;     
		server_addr.sin_port = htons(overlay_port_number);   
		server_addr.sin_addr = *((struct in_addr *)host->h_addr_list[0]);
		memset(&(server_addr.sin_zero), 0, 8); 
		
		if (connect(overlay_sock, (struct sockaddr *)&server_addr,
					sizeof(struct sockaddr)) == -1) 
		{
			fprintf(stderr, "Error connecting to server: %i\n", socket_error());
			overlay_port_number = -1;
			return -1;
		}
		
		return overlay_sock;
	}
	else 
	{
		return -1;
	}
}

static void close_socket()
{
	closesocket(overlay_sock);
	overlay_sock = -1;
}

static OverlayResponse *last_response;

static OverlayResponse *get_overlay(OverlayRequest *request)
{
	int sock;
	
	if (last_response != NULL && last_response->valid_until_timestamp >= request->timestamp)
	{
		return last_response;
	}
	overlay_response_free(last_response);
	last_response = NULL;
	
	sock = get_socket();
	
	if (sock != -1)
	{
		if (send_overlay_request(sock, request) == -1)
		{
			fprintf(stderr, "Error sending overlay request: %i\n", socket_error());
			overlay_port_number = -1;
			close_socket();
		}
		else 
		{
			last_response = overlay_response_init();
			if (read_overlay_response(sock, last_response) == -1)
			{
				fprintf(stderr, "Error reading overlay request: %i\n", socket_error());
				overlay_response_free(last_response);
				last_response = NULL;
				overlay_port_number = -1;
				close_socket();
			}
		}
	}

	
	return last_response;
}


static inline unsigned char blend(unsigned char v1, unsigned char v2, unsigned alpha)
{
	if (alpha == 0)
	{
		return v1;
	}
	else if (alpha == 255)
	{
		return v2;
	}
	else if (alpha == 127)
	{
		return (v1 >> 1) + (v2 >> 1);		
	}
	else 
	{
		return (v1 * (255 - alpha) + v2 * alpha) >> 8;
	}
	
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static void blend_overlay(AVPicture *pic, enum PixelFormat pf, int width, int height, Overlay *overlay)
{
    int channel;
	int stride;
    unsigned char *row[4];
	int max_x = MIN(overlay->width, width - overlay->left);
	int max_y = MIN(overlay->height, height - overlay->top);

	int vsub, hsub;
	
	avcodec_get_chroma_sub_sample(pf, &hsub, &vsub);
	
	stride = overlay->width * 4;
	
	for (int y = 0; y < max_y; ++y)
	{
		int dest_y = y + overlay->top;
		row[0] = pic->data[0] + dest_y  * pic->linesize[0];

		for (channel = 1; channel < 3; ++channel)
		{
			row[channel] = pic->data[channel] +
			pic->linesize[channel] * (dest_y >> vsub);
		}

		for (int x = 0; x < max_x; ++x)
		{
			uint8_t *rgba_color = overlay->data +  y * stride + x * 4;			
			uint8_t alpha = rgba_color[0];

			if (alpha > 0)
			{			
				uint8_t _y  = RGB_TO_Y(rgba_color[1], rgba_color[2], rgba_color[3]);
				uint8_t _cb = RGB_TO_U(rgba_color[1], rgba_color[2], rgba_color[3], 0);
				uint8_t _cr = RGB_TO_V(rgba_color[1], rgba_color[2], rgba_color[3], 0);

				int dest_x = x + overlay->left;

				int hx = 1 << hsub;
				int hy = 1 << vsub;

				row[0][dest_x] = blend(row[0][dest_x], _y, alpha);

				if (dest_y % hy == 0 && dest_x % hx == 0)
				{
					row[1][dest_x >> hsub] = blend(row[1][dest_x >> hsub], _cb, alpha);
					row[2][dest_x >> hsub] = blend(row[2][dest_x >> hsub], _cr, alpha);
				}
			}
		}
	}
}

static void process_frame(AVPicture *pic, enum PixelFormat pf, int width, int height, uint64_t timestamp)
{
	OverlayRequest req;
	OverlayResponse *resp;
	
	req.conversion_id = overlay_conversion_id;
	req.width = width;
	req.height = height;
	req.timestamp = timestamp;

	resp = get_overlay(&req);

	if (resp != NULL && resp->overlay_count > 0)
	{
		for (int i = 0; i < resp->overlay_count; ++i)
		{
			blend_overlay(pic, pf, width, height, resp->overlays[i]);
		}
	}
}

int64_t seeked_to = 0;

static int av_seek_frame2(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
	seeked_to = timestamp;
	return av_seek_frame(s, stream_index, timestamp, flags);
}

#define av_seek_frame av_seek_frame2

#define fix_resample() ost->video_resample = 1;

#define blend_subtitle() process_frame((AVPicture*)final_picture, enc->pix_fmt, enc->width, enc->height, (ist->pts + seeked_to) * 1000 / AV_TIME_BASE);

#define report_padding() do_report_padding(output_files);

int ffmpeg_main(int argc, char **argv);


static void preprocess_arguments(int *argc, char ***argv)
{
	char **res = av_malloc(*argc * sizeof(char*));
	int index = 0;
	
	for (int i = 0; i < *argc; ++i)
	{
		if (strcmp((*argv)[i], "--conversion-id") == 0)
		{
			overlay_conversion_id = (*argv)[i+1];
			i += 1;
			continue;
		} 
		else if (strcmp((*argv)[i], "--port-number") == 0)
		{
			sscanf((*argv)[i+1], "%i", &overlay_port_number);
			i += 1;
			continue;
		}
				else if (strcmp((*argv)[i], "--segment-length") == 0)
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
		
		res[index++] = (*argv)[i];
		
	}	
	*argv = res;
	*argc = index;
}


int main(int argc, char **argv)
{
	int res;	
	init_sockets();
	preprocess_arguments(&argc, &argv);
	res = ffmpeg_main(argc, argv);
	close_socket();
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


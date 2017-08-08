#include <ctype.h>
#include "avformat.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "htl.h"

typedef struct {
  AVFormatContext *s;
  int width;
  int height;
  int frame_cnt;
  int frame_size;
  char *frame_buf;
} HTLContext;

static int htl_error(HTLContext *cont, const char *fmt, ...)
  __attribute__ ((format (printf, 2, 3)));

static int htl_error(HTLContext *cont, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  av_vlog(cont->s, AV_LOG_ERROR, fmt, ap);
  va_end(ap);
  return AVERROR(EINVAL);
}


static int htl_write_header(AVFormatContext *s)
{
  HTLContext *cont = (HTLContext *)s->priv_data;
  AVIOContext *pb = s->pb;
  AVStream  *st = s->streams[0];
  // AVCodecContext *codec = st->codec;
  AVCodecParameters *codec = st->codecpar;
  htl_header_t header;
  
  cont->s = s;
  cont->width  = codec->width;
  cont->height = codec->height;
  cont->frame_cnt = 0;
  cont->frame_size = cont->width * cont->height * 2;
  cont->frame_buf = av_malloc(cont->frame_size);
  if(cont->frame_buf == NULL)
    return htl_error(cont, "malloc %d bytes failed.\n",cont->frame_size);
  
  memset(&header, 0, sizeof header);
  strcpy(header.signature, "HTL");
  header.size            = sizeof(header);
  header.version	 = 1;
  header.type            = 1;
  header.width		 = cont->width;
  header.height		 = cont->height;
  header.fps_numerator   = st->avg_frame_rate.num;
  header.fps_denominator = st->avg_frame_rate.den;
  header.frames = st->nb_frames;
  header.frame_size = cont->frame_size;
  header.frame_start = 512;
  header.frame_offset = cont->frame_size;

  avio_write(pb, (const unsigned char *)&header, sizeof(header));
  avio_seek(pb, header.frame_start, SEEK_SET);
  return 0;
}

static int htl_write_video(AVFormatContext *s, AVCodecContext *enc,
			   AVPacket *pkt)
{
  const uint8_t *buf = pkt->data;
  HTLContext *cont = (HTLContext *)s->priv_data;
  AVIOContext *pb = s->pb;
  int w = cont->width;
  int h = cont->height;
  int x,y;
  const uint8_t *p = buf;
  uint8_t *q = cont->frame_buf;
  
  memset(cont->frame_buf, 0, cont->frame_size);
  for(y=0; y<h; y++){
    for(x=0; x<w; x++){
      uint16_t r = *p++;
      uint16_t g = *p++;
      uint16_t b = *p++;
      uint16_t rgb;
      p++;
      rgb = ((r & 0xf8)<<8)|((g & 0xfc)<<3)|((b & 0xf8) >> 3);
      *q++ = rgb >> 8;
      *q++ = rgb;
    }
  }
  avio_write(pb, cont->frame_buf, cont->frame_size);
  cont->frame_cnt++;
  return 0;
}

static int htl_write_packet(AVFormatContext *s, AVPacket *pkt)
{
  AVCodecContext *codec = s->streams[pkt->stream_index]->codec;

  if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
    return 0; /* just ignore audio */
  else
    return htl_write_video(s, codec, pkt);
  // return htl_write_video(s, codec, pkt->data, pkt->size);
}

static int htl_write_trailer(AVFormatContext *s)
{
  avio_flush(s->pb);
  return 0;
}


static const AVClass htl_muxer_class = {
    .class_name = "HTL muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVOutputFormat ff_htl_muxer = {
    .name           = "htl",
    .long_name      = NULL_IF_CONFIG_SMALL("Humblesoft LED Movie"),
    .mime_type      = "video/htl",
    .extensions     = "htl",
    .priv_data_size = sizeof(HTLContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = htl_write_header,
    .write_packet   = htl_write_packet,
    .write_trailer  = htl_write_trailer,
    .priv_class     = &htl_muxer_class,
};


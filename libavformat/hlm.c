#include <ctype.h>
#include "avformat.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "hlm.h"

typedef enum {
  lmt_32x16s8,
  lmt_32x32s16,
  lmt_64x32s16,
} lm_type_t;


#define MASK_MAX	6

typedef struct {
  AVFormatContext *s;
  char *led_scan;	/* option: -hlm_scan */
  char *led_bits;	/* option: -hlm_bits */
  char *offset;		/* option: -hlm_offset */

  int w;
  int h;
  int ox,oy;
  int srow;		/* scan row  */
  int lrpm;		/* Led Row per module */
  int bpp;		/* byte per plane */
  int mask[MASK_MAX];	/* bit Position for R0,G0,B0,R1,B1,B1 */
  int frame_cnt;
  int frame_size;
  char *frame_buf;
  hlm3_header_t header;
} HLMContext;

static int hlm_error(HLMContext *cont, const char *fmt, ...)
  __attribute__ ((format (printf, 2, 3)));

static int hlm_error(HLMContext *cont, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  av_vlog(cont->s, AV_LOG_ERROR, fmt, ap);
  va_end(ap);
  return AVERROR(EINVAL);
}


static int parse_led_scan(HLMContext *cont)
{
  int scan;
  
  if(sscanf(cont->led_scan, "%d", &scan) != 1)
    return hlm_error(cont, "bad integer %s", cont->led_scan);

  if(scan != 8 && scan != 16 && scan != 32)
    return hlm_error(cont, "led_scan must be 8,16 or 32");

  cont->srow = scan;
  cont->lrpm = scan * 2;;
  return 0;
}

static int parse_led_bits(HLMContext *cont)
{
  char *p = cont->led_bits;
  int  i = 0;
  int  b,b1,b2;

  if(*p == 0)
    return hlm_error(cont, "-hlm_bits not defined.");
  
  while(1){
    if(!isdigit((int)*p))
      return hlm_error(cont, "number expacted, at %s\n", p);
    b1 = *p++ - '0';
    while(isdigit((int)*p))
      b1 = 10*b1 + *p++ - '0'; 
    if(b1 > 15)
      return hlm_error(cont, "Too big bit position:%d\n",b1);
    if(*p == '-'){
      if(!isdigit((int)*++p))
	return hlm_error(cont, "number expected, at %s\n",p-1);
      b2 = *p++ - '0';
      while(isdigit((int)*p))
	b2 = 10 * b2 + *p++ - '0';
      if(b2 > 15)
	return hlm_error(cont, "Too big bit position:%d.\n",b2);
    }
    else b2 = b1;
    
    for(b = b1;1;b = b < b2 ? b+1 : b-1){
      if(i < MASK_MAX)
	cont->mask[i++] = 1 << b;
      else
	return hlm_error(cont,"Too many bits, %d bits needed.\n", MASK_MAX);
      if(b == b2) break;
    }
    if(*p == 0) break;
    else if(*p++ == ',') continue;
    else return hlm_error(cont,"Unexpected char at:%s\n",p-1);
  }
  if(i < MASK_MAX)
    return hlm_error(cont,"%d bit position found,%d needed.\n",i,MASK_MAX);

  return 0;
}

static int parse_offset(HLMContext *cont)
{
  char *p = cont->offset;

  if(*p == 0)
    cont->ox = cont->oy = 0;
  else if(sscanf(p,"%d,%d", &cont->ox, &cont->oy) != 2)
    return hlm_error(cont,"bad offset value:%s, should be ox,oy format.\n",p);

  return 0;
}

static int parse_options(HLMContext *cont)
{
  int r;
  // led_confの解釈
  r = parse_led_scan(cont);
  if(r) return r;
  
  // led_bitsの解釈
  r = parse_led_bits(cont);
  if(r) return r;

  // offsetの解釈
  r = parse_offset(cont);
  if(r) return r;
  
  return 0;
}

#define NS(s)  ((s)==NULL ? "null" : s)

static uint8_t *hlm_buf_ptr(HLMContext *cont,int x,int y,int *ppi)
{
  uint8_t *p = cont->frame_buf;
  int xx,yy,ym;
  
  if(x < 0 || x >= cont->w || y < 0 || y >= cont->h)
    return NULL;

  ym = y / cont->lrpm;
  yy = y % cont->lrpm;
  xx = ym * cont->w + x;

  if(ppi)
    *ppi = yy < cont->srow ? 0 : 1;
  
  yy %= cont->srow;
  p += yy * cont->w * (cont->h / cont->lrpm) + xx;
  
  return p;
}

static void hlm_pset(HLMContext *cont,int x,int y,const uint8_t *rgb)
{
  int m,m2,c,pi;
  uint8_t *p = hlm_buf_ptr(cont, x + cont->ox, y + cont->oy, &pi);
  uint8_t *s = (uint8_t *)cont->frame_buf;
  uint8_t *e = s + cont->frame_size;
  
  if(!p) return;
  for(m = 0x80; m; m >>= 1){
    for(c=0; c<3; c++){
      m2 = cont->mask[c + pi*3];
      if(p >= s && p < e){
	if(rgb[c] & m)
	  *p |= m2;
	else
	  *p &= ~m2;
      } else {
	fprintf(stderr,"Bad m:%x ptr:%p , s:%p e:%p\n", m, p, s, e); 
      }
    }
    p += cont->bpp;
  }
}

static int hlm_write_header(AVFormatContext *s)
{
  HLMContext *cont = (HLMContext *)s->priv_data;
  AVIOContext *pb = s->pb;
  AVStream  *st = s->streams[0];
  AVCodecParameters *codecpar = st->codecpar;
  hlm3_header_t *header = &cont->header;
  int r;
  
  r = parse_options(cont);
  if(r) return r;
  
  cont->s = s;
  cont->w = codecpar->width;
  cont->h = codecpar->height;

  if(cont->h % (cont->lrpm))
    return hlm_error(cont,"height must be multiple of %d",cont->lrpm);
  
  cont->bpp = cont->w * cont->h / 2;
  cont->frame_size = cont->bpp * 8;
  cont->frame_cnt = 0;

  cont->frame_buf = av_malloc(cont->frame_size);
  if(cont->frame_buf == NULL)
    return hlm_error(cont, "malloc %d bytes failed.\n",cont->frame_size);

  memset(header, 0, sizeof *header);
  strcpy(header->signature, "HLM");
  header->size            = sizeof(header);
  header->version	 = 3;
  header->width		 = cont->w;
  header->height		 = cont->h;
  header->scan            = cont->srow;
  header->fps_numerator   = st->avg_frame_rate.num;
  header->fps_denominator = st->avg_frame_rate.den;
  header->frames = 0;
  header->frame_size = cont->frame_size;
  header->frame_start = 512;
  header->frame_offset = cont->frame_size;

  avio_write(pb, (const unsigned char *)&header, sizeof(header));
  avio_seek(pb, header->frame_start, SEEK_SET);
  return 0;
}

static int hlm_write_video(AVFormatContext *s, AVCodecParameters *enc,
			   AVPacket *pkt)
{
  const uint8_t *buf = pkt->data;
  HLMContext *cont = (HLMContext *)s->priv_data;
  AVIOContext *pb = s->pb;
  int w = cont->w;
  int h = cont->h;
  int x,y;
  const uint8_t *p = buf;

  memset(cont->frame_buf, 0, cont->frame_size);
  for(y=0; y<h; y++){
    for(x=0; x<w; x++){
      hlm_pset(cont, x, y, p);
      p += 4;
    }
  }
  avio_write(pb, cont->frame_buf, cont->frame_size);
  cont->frame_cnt++;
  return 0;
}

static int hlm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
  AVCodecParameters *codecpar = s->streams[pkt->stream_index]->codecpar;

  if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    return 0; /* just ignore audio */
  else
    return hlm_write_video(s, codecpar, pkt);
}

static int hlm_write_trailer(AVFormatContext *s)
{
  HLMContext *cont = (HLMContext *)s->priv_data;
  avio_flush(s->pb);

  cont->header.frames = cont->frame_cnt;
  avio_seek(s->pb, 0, SEEK_SET);
  avio_write(s->pb, (const unsigned char *)&cont->header,
             sizeof(cont->header));
  
  return 0;
}

#define OFFSET(x) offsetof(HLMContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
  {"hlm_scan", "specify LED scan", OFFSET(led_scan),
   AV_OPT_TYPE_STRING, {.str = "16"}, 0, 0, ENC} ,
  {"hlm_bits", "specify bits allocation on port", OFFSET(led_bits),
   AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, ENC} ,
  {"hlm_offset", "specify output image position offset", OFFSET(offset),
   AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, ENC} ,
  { NULL },
};

static const AVClass hlm_muxer_class = {
    .class_name = "HLM muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = options,
};


AVOutputFormat ff_hlm_muxer = {
    .name           = "hlm",
    .long_name      = NULL_IF_CONFIG_SMALL("Humblesoft LED Movie"),
    .mime_type      = "video/hlm",
    .extensions     = "hlm",
    .priv_data_size = sizeof(HLMContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = hlm_write_header,
    .write_packet   = hlm_write_packet,
    .write_trailer  = hlm_write_trailer,
    .priv_class     = &hlm_muxer_class,
};


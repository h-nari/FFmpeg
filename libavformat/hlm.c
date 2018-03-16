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

typedef struct {
  const char 	*id;
  const char 	*name;
  lm_type_t 	lm_type;
  int		mcol;		/* led module columns */
  int		mrow;		/* led module rows */
  int	        turnback;	/* turnback */
} lm_config_t;

const lm_config_t lm_config_tbl[] =  {
  { "A1",	"SF3216 1 x 1",		lmt_32x16s8, 1, 1},
  { "A2",	"SF3216 2 x 1",		lmt_32x16s8, 2, 1},
  { "A3",	"SF3216 3 x 1",		lmt_32x16s8, 3, 1},
  { "A4",	"SF3216 4 x 1",		lmt_32x16s8, 4, 1},
  { "A6",	"SF3216 6 x 1",		lmt_32x16s8, 6, 1},
  { "A8",	"SF3216 8 x 1",		lmt_32x16s8, 8, 1},
  
  { "B1",	"SF3232 1 x 1",		lmt_32x32s16, 1, 1},
  { "B2",	"SF3232 2 x 1", 	lmt_32x32s16, 2, 1},
  { "B22",	"SF3232 2 x 2", 	lmt_32x32s16, 2, 2},
  { "B23",	"SF3232 3 x 2", 	lmt_32x32s16, 3, 2},
  { "B4",	"SF3232 4 x 1", 	lmt_32x32s16, 4, 1},
  { "B43",	"SF3232 4 x 3", 	lmt_32x32s16, 4, 3},
  { "B6",	"SF3232 6 x 1", 	lmt_32x32s16, 6, 1},

  { "C1",	"64x32 1x1",		lmt_64x32s16, 1, 1},
  { "C12",	"64x32 1x2",		lmt_64x32s16, 1, 2},
  { "C13",	"64x32 1x3",		lmt_64x32s16, 1, 3},
  { "C12T",	"64x32 1x2",		lmt_64x32s16, 1, 2, 1},
  { "C13T",	"64x32 1x3",		lmt_64x32s16, 1, 3, 1},
  { NULL},
};


#define MASK_MAX	6

typedef struct {
  AVFormatContext *s;
  char *led_conf;	/* option: -hlm_conf */
  char *led_bits;	/* option: -hlm_bits */
  char *offset;		/* option: -hlm_offset */

  int src_w;
  int src_h;
  int dst_w;
  int dst_h;
  int ox,oy;
  int srow;		/* scan row  */
  int lrpm;		/* Led Row per module */
  int bpp;		/* byte per plane */
  int turnback;
  int mask[MASK_MAX];	/* bit Position for R0,G0,B0,R1,B1,B1 */
  int frame_cnt;
  int frame_size;
  char *frame_buf;
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


static int parse_led_conf(HLMContext *cont)
{
  int lcpm;		/* led column per module */
  const lm_config_t *p;
  
  for(p=lm_config_tbl; p->id; p++){
    if(strcmp(cont->led_conf, p->id)==0)
      break;
  }
  if(p->id == NULL)
    return hlm_error(cont, "LED conf \"%s\" not defined.\n", cont->led_conf);

  if(p->lm_type == lmt_32x16s8){ 
    lcpm = 32;
    cont->srow = 8;
    cont->lrpm = 16;
  }
  else if(p->lm_type == lmt_32x32s16){
    lcpm = 32;
    cont->srow = 16;
    cont->lrpm = 32;
  }
  else if(p->lm_type == lmt_64x32s16){
    lcpm = 64;
    cont->srow = 16;
    cont->lrpm = 32;
  }
  else return hlm_error(cont, "undefined lm_type:%d\n", p->lm_type);

  cont->dst_w = p->mcol * lcpm;
  cont->dst_h = p->mrow * cont->lrpm;
  cont->bpp = cont->dst_w * cont->dst_h / 2;
  cont->frame_size = cont->bpp * 8;
  cont->turnback = p->turnback;
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
  r = parse_led_conf(cont);
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
  int xx,yy,ym,ym2;
  
  if(x < 0 || x >= cont->dst_w || y < 0 || y >= cont->dst_h)
    return NULL;

  ym = y / cont->lrpm;
  yy = y % cont->lrpm;
  ym2 = (cont->dst_h - y - 1) / cont->lrpm;
  xx = ym * cont->dst_w;
  if(cont->turnback && (ym2 & 1)){
    xx += cont->dst_w - x - 1;
    yy = cont->lrpm - yy - 1;
  } else {
    xx += x;
  }

  if(ppi)
    *ppi = yy < cont->srow ? 0 : 1;
  
  yy %= cont->srow;
  p += yy * cont->dst_w * (cont->dst_h / cont->lrpm) + xx;
  
#if 0
  fprintf(stderr,"x:%d y:%d lprm:%d ym:%d ym2:%d\n",x,y,cont->lrpm, ym,ym2);
  fprintf(stderr,"turnback:%d xx:%d srow:%d\n",cont->turnback, xx,cont->srow);
  fprintf(stderr,"offset:%d\n", p - (uint8_t *)cont->frame_buf);
  exit(1);
#endif
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
  AVCodecContext *codec = st->codec;
  hlm_header_t header;
  int r;
  
  cont->s = s;
  cont->src_w = codec->width;
  cont->src_h = codec->height;
  cont->frame_cnt = 0;
  fprintf(stderr," size %d x %d\n", codec->width, codec->height);

  r = parse_options(cont);
  if(r) return r;
  cont->frame_buf = av_malloc(cont->frame_size);
  if(cont->frame_buf == NULL)
    return hlm_error(cont, "malloc %d bytes failed.\n",cont->frame_size);

  fprintf(stderr," led_conf:%s turnback:%d\n",cont->led_conf, cont->turnback);
  
  memset(&header, 0, sizeof header);
  strcpy(header.signature, "HLM");
  header.size            = sizeof(header);
  header.version	 = 2;
  header.type            = 1;
  header.width		 = cont->dst_w;
  header.height		 = cont->dst_h;
  header.fps_numerator   = st->avg_frame_rate.num;
  header.fps_denominator = st->avg_frame_rate.den;
  header.frames = st->nb_frames;
  header.frame_size = cont->frame_size;
  header.frame_start = 512;
  header.frame_offset = cont->frame_size;
  strncpy(header.led_conf, cont->led_conf, sizeof header.led_conf);

  avio_write(pb, (const unsigned char *)&header, sizeof(header));
  avio_seek(pb, header.frame_start, SEEK_SET);
  return 0;
}

static int hlm_write_video(AVFormatContext *s, AVCodecContext *enc,
			   AVPacket *pkt)
{
  const uint8_t *buf = pkt->data;
  HLMContext *cont = (HLMContext *)s->priv_data;
  AVIOContext *pb = s->pb;
  int w = cont->src_w;
  int h = cont->src_h;
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
  AVCodecContext *codec = s->streams[pkt->stream_index]->codec;

  if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
    return 0; /* just ignore audio */
  else
    return hlm_write_video(s, codec, pkt);
  // return hlm_write_video(s, codec, pkt->data, pkt->size);
}

static int hlm_write_trailer(AVFormatContext *s)
{
  avio_flush(s->pb);
  return 0;
}

#define OFFSET(x) offsetof(HLMContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
  {"hlm_conf", "specify LED Configuration", OFFSET(led_conf),
   AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, ENC} ,
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


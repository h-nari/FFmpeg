#ifndef _htl_h
#define _htl_h

/*
 * Humblesoft TFT LCD format
 */

typedef struct {
  char signature[4];		/* HTL */
  uint16_t size;		/* この構造体のサイズ */
  uint16_t version;		/* タイプ */
  uint16_t type;		/* タイプ */
  uint16_t width;
  uint16_t height;
  uint16_t fps_numerator;
  uint16_t fps_denominator;
  uint32_t frames;		/* フレーム数 */
  uint32_t flags;
  uint32_t frame_size;		/* 1フレームデータのバイト数　　　 */	
  uint32_t frame_start;		/* 最初のフレームデータのオフセット */
  uint32_t frame_offset;	/* フレームごとのオフセット	　　　 */
} htl_header_t;



#endif /* _htl_h */

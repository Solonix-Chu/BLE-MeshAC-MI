#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG__SPEED3_ALPHA_15X15
#define LV_ATTRIBUTE_IMG__SPEED3_ALPHA_15X15
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG__SPEED3_ALPHA_15X15 uint8_t _speed3_alpha_15x15_map[] = {
  0x00, 0x00, 0x00, 0x0a, 	/*Color of index 0*/
  0x00, 0x00, 0x00, 0xd5, 	/*Color of index 1*/

  0x01, 0x80, 
  0x07, 0x80, 
  0x06, 0x00, 
  0x04, 0x00, 
  0xc6, 0x38, 
  0x42, 0x6c, 
  0xc0, 0x04, 
  0x78, 0x00, 
  0x10, 0x40, 
  0x00, 0x60, 
  0x02, 0x10, 
  0x26, 0x10, 
  0x3c, 0x30, 
  0x00, 0x60, 
  0x00, 0x00, 
};

const lv_img_dsc_t _speed3_alpha_15x15 = {
  .header.cf = LV_IMG_CF_INDEXED_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 15,
  .header.h = 15,
  .data_size = 38,
  .data = _speed3_alpha_15x15_map,
};

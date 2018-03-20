#ifndef _JPEG_H_
#define _JPEG_H_

#ifdef __cplusplus
extern "C" {
#endif

uint8_t *yuyv2rgb(uint8_t * yuyv, uint32_t width, uint32_t height);
void jpeg(FILE * dest, uint8_t * rgb, uint32_t width, uint32_t height, int quality);

#ifdef __cplusplus
}
#endif

#endif  //end of _JPEG_H_
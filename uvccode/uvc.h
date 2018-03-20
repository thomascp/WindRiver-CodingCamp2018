#ifndef _UVC_H_
#define _UVC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
} io_method;

typedef struct
{
    uint8_t *start;
    size_t   length;
} buffer_t;


typedef struct
{
    int       fd;
    io_method io;
    uint32_t  width;
    uint32_t  height;
    size_t    buffer_count;
    buffer_t *buffers;
    buffer_t  head;
} camera_t;

#ifdef __cplusplus
}
#endif

#endif  //end of _UVC_H_

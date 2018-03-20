#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include"uvc.h"
#include"jpeg.h"
#define UVC_DEVICE "/dev/video0"
#define UVC_MAX_COUNT 4

static int frame_number = 0;

static int xioctl(int fh, int request, void *arg)
{
    int r;

    do
    {
        r = ioctl(fh, request, arg);
    }
    while (-1 == r && EINTR == errno);

    return r;
}

static void process_image(uint8_t *start, int size, int width, int height)
{
    uint8_t *rgb;

    frame_number++;
    char     filename[15];
    sprintf(filename, "unknown-person.jpg");
    FILE    *fp = fopen(filename, "wb");

    rgb = yuyv2rgb(start, width, height);
    jpeg(fp, rgb, width, height, 30);

    free(rgb);
    fclose(fp);
}

int open_device(camera_t *camera, const char *device)
{
    if((camera == NULL) || (device == NULL))
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    camera->fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (camera->fd < 0)
    {
        printf("(%s)(%d)open device fail (%s)\r\n", __FUNCTION__, __LINE__, device);
        return -1;
    }

    return 0;
}

static int init_userp(camera_t *camera, unsigned int buffer_size)
{
    struct v4l2_requestbuffers req;
    int i = 0;

    memset(&req, 0x00, sizeof(struct v4l2_requestbuffers));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    req.count  = UVC_MAX_COUNT;


    if (-1 == xioctl(camera->fd, VIDIOC_REQBUFS, &req))
    {
        printf("(%s)(%d) VIDIOC_REQBUFS failure\r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    if (req.count < UVC_MAX_COUNT)
    {
        printf("(%s)(%d) Insufficient buffer memory \r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    camera->buffers = calloc(req.count, sizeof(*camera->buffers));
    if (!camera->buffers)
    {
        printf("(%s)(%d) out of memory \r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    for (i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buffer;
        memset(&buffer, 0x00, sizeof(struct v4l2_buffer));

        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_USERPTR;
        buffer.index  = i;

        if (-1 == xioctl(camera->fd, VIDIOC_QUERYBUF, &buffer))
        {
            printf("(%s)(%d) VIDIOC_QUERYBUF failure\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        camera->buffers[i].length = buffer.length;
        camera->buffers[i].start  = malloc(buffer.length);
        if(!camera->buffers[i].start)
        {
            printf("(%s)(%d) out of memory \r\n", __FUNCTION__, __LINE__);
            return -1;
        }		
    }

    camera->buffer_count = req.count;
    return 0;
}

static int init_mmap(camera_t *camera)
{
    struct v4l2_requestbuffers req;
    int i = 0;

    memset(&req, 0x00, sizeof(struct v4l2_requestbuffers));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = UVC_MAX_COUNT;

    if (-1 == xioctl(camera->fd, VIDIOC_REQBUFS, &req))
    {
        printf("(%s)(%d) VIDIOC_REQBUFS failure\r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    if (req.count < UVC_MAX_COUNT)
    {
        printf("(%s)(%d) Insufficient buffer memory \r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    camera->buffers = calloc(req.count, sizeof(*camera->buffers));
    if (!camera->buffers)
    {
        printf("(%s)(%d) out of memory \r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    for (i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buffer;
        memset(&buffer, 0x00, sizeof(struct v4l2_buffer));

        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index  = i;

        if (-1 == ioctl(camera->fd, VIDIOC_QUERYBUF, &buffer))
        {
            printf("(%s)(%d) VIDIOC_QUERYBUF failure\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        camera->buffers[i].length = buffer.length;
        camera->buffers[i].start  = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE
                                         , MAP_SHARED, camera->fd, buffer.m.offset);
    }
    camera->buffer_count = req.count;

    return 0;
}

int init_device(camera_t *camera)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if (-1 == xioctl(camera->fd, VIDIOC_QUERYCAP, &cap))
    {
        printf("(%s)(%d) VIDIOC_QUERYCAP failure\r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        printf("(%s)(%d) no capture device\r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    printf("driver name\t: %s\n", cap.driver);
    printf("card name\t: %s\n", cap.card);
    printf("bus info\t: %s\n", cap.bus_info);

    switch(camera->io)
    {
    case IO_METHOD_READ:
        if (!(cap.capabilities & V4L2_CAP_READWRITE))
        {
            printf("(%s)(%d) does not support read/write i/o \r\n", __FUNCTION__, __LINE__);
            return -1;
        }
        break;
    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        if (!(cap.capabilities & V4L2_CAP_STREAMING))
        {
            printf("(%s)(%d) does not support streaming i/o \r\n", __FUNCTION__, __LINE__);
            return -1;
        }
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    /* Get format */
    memset(&fmt, 0x00, sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(camera->fd, VIDIOC_G_FMT, &fmt))
    {
        printf("(%s)(%d) VIDIOC_G_FMT failure\r\n", __FUNCTION__, __LINE__);
        return -1;
    }
    camera->width  = fmt.fmt.pix.width;
    camera->height = fmt.fmt.pix.height;
    printf("Max Width: %4d, Height: %4d\n", camera->width, camera->height);

    /* Set format */
    memset(&fmt, 0x00, sizeof(struct v4l2_format));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = camera->width;
    fmt.fmt.pix.height      = camera->height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (-1 == xioctl(camera->fd, VIDIOC_S_FMT, &fmt))
    {
        printf("(%s)(%d) VIDIOC_S_FMT failure\r\n", __FUNCTION__, __LINE__);
        return -1;
    }

    /* Request buffers */

    switch(camera->io)
    {
    case IO_METHOD_READ:

        break;
    case IO_METHOD_MMAP:
        init_mmap(camera);
        break;
    case IO_METHOD_USERPTR:
        init_userp(camera, fmt.fmt.pix.sizeimage * 1.5);
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }
    return 0;
}

int start_capturing(camera_t *camera)
{
    int i;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    switch(camera->io)
    {
    case IO_METHOD_READ:
        break;
    case IO_METHOD_MMAP:
        for (i = 0; i < camera->buffer_count; i++)
        {
            struct v4l2_buffer buffer;
            memset(&buffer, 0x00, sizeof(struct v4l2_buffer));

            buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index  = i;

            if (-1 == xioctl(camera->fd, VIDIOC_QBUF, &buffer))
            {
                printf("(%s)(%d) VIDIOC_QBUF failure\r\n", __FUNCTION__, __LINE__);
                return -1;
            }
        }

        if (-1 == xioctl(camera->fd, VIDIOC_STREAMON, &type))
        {
            printf("(%s)(%d) VIDIOC_STREAMON failure\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        break;
    case IO_METHOD_USERPTR:
        for (i = 0; i < camera->buffer_count; i++)
        {
            struct v4l2_buffer buffer;
            memset(&buffer, 0x00, sizeof(struct v4l2_buffer));

            buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_USERPTR;
            buffer.index  = i;
            buffer.m.userptr = (unsigned long) camera->buffers[i].start;
            buffer.length = camera->buffers[i].length;


            if (-1 == xioctl(camera->fd, VIDIOC_QBUF, &buffer))
            {
                printf("(%s)(%d) VIDIOC_QBUF failure\r\n", __FUNCTION__, __LINE__);
                return -1;
            }
        }

        if (-1 == xioctl(camera->fd, VIDIOC_STREAMON, &type))
        {
            printf("(%s)(%d) VIDIOC_STREAMON failure\r\n", __FUNCTION__, __LINE__);
            return -1;
        }
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }
    return 0;
}

int read_frame(camera_t *camera)
{
    struct v4l2_buffer buf;
    unsigned int i = 0;

    switch(camera->io)
    {
    case IO_METHOD_READ:

        break;
    case IO_METHOD_MMAP:
        memset(&buf, 0x00, sizeof(struct v4l2_buffer));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(camera->fd, VIDIOC_DQBUF, &buf))
        {
            printf("(%s)(%d)VIDIOC_DQBUF fail\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        process_image(camera->buffers[buf.index].start, buf.bytesused, camera->width, camera->height);

        if (-1 == xioctl(camera->fd, VIDIOC_QBUF, &buf))
        {
            printf("(%s)(%d)VIDIOC_DQBUF fail\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        break;
    case IO_METHOD_USERPTR:
        memset(&buf, 0x00, sizeof(struct v4l2_buffer));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(camera->fd, VIDIOC_DQBUF, &buf))
        {
            printf("(%s)(%d)VIDIOC_DQBUF fail\r\n", __FUNCTION__, __LINE__);
            return -1;
        }

        for(i=0; i<camera->buffer_count ; i++)
        {
            if(buf.m.userptr == (unsigned long) camera->buffers[i].start &&
                    buf.length == camera->buffers[i].length)
                break;
        }

        process_image((uint8_t *)buf.m.userptr , buf.bytesused, camera->width, camera->height);

        if (-1 == xioctl(camera->fd, VIDIOC_QBUF, &buf))
        {
            printf("(%s)(%d)VIDIOC_DQBUF fail\r\n", __FUNCTION__, __LINE__);
            return -1;
        }
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }



    return 0;
}

int mainloop(camera_t *camera)
{
    fd_set fds;
    struct timeval tv;
    int r;
    int i = 0;

    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    FD_ZERO(&fds);
    FD_SET(camera->fd, &fds);

    for (i = 0; i < 80;i++)
    {
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        r = select(camera->fd + 1, &fds, NULL, NULL, &tv);
        switch(r)
        {
        case -1:
            printf("(%s)(%d) select error\r\n",__FUNCTION__,__LINE__);
            break;
        case 0:
            printf("(%s)(%d) select timeout\r\n",__FUNCTION__,__LINE__);
            break;
        default:
            read_frame(camera);
            break;
        }
    }
    return 0;
}

int stop_capturing(camera_t *camera)
{
    enum v4l2_buf_type type;

    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }


    switch(camera->io)
    {
    case IO_METHOD_READ:
        break;
    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (-1 == xioctl(camera->fd, VIDIOC_STREAMOFF, &type))
        {
            printf("(%s)(%d) VIDIOC_STREAMOFF failure\r\n", __FUNCTION__, __LINE__);
            return -1;
        }
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }
    return 0;
}

int uninit_device(camera_t *camera)
{
    unsigned int i;

    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }


    switch(camera->io)
    {
    case IO_METHOD_READ:

        break;
    case IO_METHOD_MMAP:
        for (i = 0; i < camera->buffer_count; i++)
        {
            munmap(camera->buffers[i].start, camera->buffers[i].length);
        }
        break;
    case IO_METHOD_USERPTR:
        for (i = 0; i < camera->buffer_count; i++)
        {
            free(camera->buffers[i].start);
        }
        break;
    default:
        printf("(%s)(%d) unsupport io method\r\n",__FUNCTION__,__LINE__);
        return -1;
    }
    return 0;
}

int close_device(camera_t *camera)
{
    if(camera == NULL)
    {
        printf("(%s)(%d) pararmeter null\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if (camera->buffers)
    {
        free(camera->buffers);
    }

    close(camera->fd);

    return 0;
}
int main(int argc, char **argv)
{
    camera_t camera;

    camera.io = IO_METHOD_USERPTR;

    if(open_device(&camera,UVC_DEVICE) < 0)
    {
        printf("(%s)(%d) open_device failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if(init_device(&camera) < 0)
    {
        printf("(%s)(%d) init_device failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if(start_capturing(&camera) < 0)
    {
        printf("(%s)(%d) start_capturing failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    mainloop(&camera);

    if(stop_capturing(&camera)< 0)
    {
        printf("(%s)(%d) stop_capturing failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if(uninit_device(&camera)< 0)
    {
        printf("(%s)(%d) uninit_device failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }

    if(close_device(&camera)< 0)
    {
        printf("(%s)(%d) close_device failure\r\n",__FUNCTION__,__LINE__);
        return -1;
    }
    return 0;
}

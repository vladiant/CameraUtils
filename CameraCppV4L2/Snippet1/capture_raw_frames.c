/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h> /* getopt_long() */

#include <errno.h>
#include <fcntl.h> /* low-level i/o */
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define FOUR (4)
#define ALIGN_TO_FOUR(VAL) (((VAL) + FOUR - 1) & ~(FOUR - 1))

int BMPwriter(unsigned char *pRGB, int bitNum, int width, int height,
              char *pFileName) {
  FILE *fp;
  int fileSize;
  unsigned char *pMovRGB;
  int i;
  int widthStep;

  unsigned char header[54] = {
      0x42,           // identity : B
      0x4d,           // identity : M
      0,    0, 0, 0,  // file size
      0,    0,        // reserved1
      0,    0,        // reserved2
      54,   0, 0, 0,  // RGB data offset
      40,   0, 0, 0,  // struct BITMAPINFOHEADER size
      0,    0, 0, 0,  // bmp width
      0,    0, 0, 0,  // bmp height
      1,    0,        // planes
      24,   0,        // bit per pixel
      0,    0, 0, 0,  // compression
      0,    0, 0, 0,  // data size
      0,    0, 0, 0,  // h resolution
      0,    0, 0, 0,  // v resolution
      0,    0, 0, 0,  // used colors
      0,    0, 0, 0   // important colors
  };

  widthStep = ALIGN_TO_FOUR(width * bitNum / 8);

  fileSize = ALIGN_TO_FOUR(widthStep * height) + sizeof(header);

  memcpy(&header[2], &fileSize, sizeof(int));
  memcpy(&header[18], &width, sizeof(int));
  memcpy(&header[22], &height, sizeof(int));
  memcpy(&header[28], &bitNum, sizeof(short));

  printf("written on file %s ...", pFileName);
  fp = fopen(pFileName, "wb");

  fwrite(&header[0], 1, sizeof(header), fp);

  pMovRGB = pRGB + (height - 1) * widthStep;

  for (i = 0; i < height; i++) {
    fwrite(pMovRGB, 1, widthStep, fp);
    pMovRGB -= widthStep;
  } /*for i*/

  fclose(fp);
  printf("done\n");

  return 0;
} /*BMPwriter*/

#define R_DIFF(VV) ((VV) + (103 * (VV) >> 8))
#define G_DIFF(UU, VV) (-((88 * (UU)) >> 8) - ((VV * 183) >> 8))
#define B_DIFF(UU) ((UU) + ((198 * (UU)) >> 8))

#define Y_PLUS_RDIFF(YY, VV) ((YY) + R_DIFF(VV))
#define Y_PLUS_GDIFF(YY, UU, VV) ((YY) + G_DIFF(UU, VV))
#define Y_PLUS_BDIFF(YY, UU) ((YY) + B_DIFF(UU))

#define EVEN_ZERO_ODD_ONE(XX) (((XX) & (0x01)))

/*if X > 255, X = 255; if X< 0, X = 0*/
#define CLIP1(XX) ((unsigned char)((XX & ~255) ? (~XX >> 15) : XX))

int YUYV2RGB24(unsigned char *pYUYV, int width, int height,
               unsigned char *pRGB24) {
  unsigned int i, j;

  unsigned char *pMovY, *pMovU, *pMovV;
  unsigned char *pMovRGB;

  unsigned int pitch;
  unsigned int pitchRGB;

  pitch = 2 * width;
  pitchRGB = ALIGN_TO_FOUR(3 * width);

  for (j = 0; j < height; j++) {
    pMovY = pYUYV + j * pitch;
    pMovU = pMovY + 1;
    pMovV = pMovY + 3;

    pMovRGB = pRGB24 + pitchRGB * j;

    for (i = 0; i < width; i++) {
      int R, G, B;
      int Y, U, V;

      Y = pMovY[0];
      U = *pMovU - 128;
      V = *pMovV - 128;
      R = Y_PLUS_RDIFF(Y, V);
      G = Y_PLUS_GDIFF(Y, U, V);
      B = Y_PLUS_BDIFF(Y, U);

      *pMovRGB = CLIP1(B);
      *(pMovRGB + 1) = CLIP1(G);
      *(pMovRGB + 2) = CLIP1(R);

      pMovY += 2;

      pMovU += 4 * EVEN_ZERO_ODD_ONE(i);
      pMovV += 4 * EVEN_ZERO_ODD_ONE(i);

      pMovRGB += 3;
    } /*for i*/
  }   /*for j*/

  return 0;
} /*YUYV2RGB24*/

#define MAX_STR_LEN (256)

int StoreRAWImage(unsigned char *pMappingBuffer, int width, int height,
                  unsigned int fmt, char *pFileName) {
  if (V4L2_PIX_FMT_YUYV != fmt) return -1;

  unsigned char *pRGB24;
  pRGB24 = (unsigned char *)malloc(ALIGN_TO_FOUR(3 * width) * height);

  YUYV2RGB24(pMappingBuffer, width, height, pRGB24);

  BMPwriter(pRGB24, 24, width, height, pFileName);
  free(pRGB24);
  pRGB24 = NULL;

  return 0;
} /*StoreRAWImage*/

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#ifndef V4L2_PIX_FMT_H264
#define V4L2_PIX_FMT_H264 \
  v4l2_fourcc('H', '2', '6', '4') /* H264 with start codes */
#endif

enum io_method {
  IO_METHOD_READ,
  IO_METHOD_MMAP,
  IO_METHOD_USERPTR,
};

struct buffer {
  void *start;
  size_t length;
};

static char *dev_name;
static enum io_method io = IO_METHOD_MMAP;
static int fd = -1;
struct buffer *buffers;
static unsigned int n_buffers;
static int out_buf;
static int force_format;
static int frame_count = 200;
static int frame_number = 0;

struct v4l2_format gFmt;

static void errno_exit(const char *s) {
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

static void process_image(const void *p, int size) {
  frame_number++;
  char filename[15];

  sprintf(filename, "frame-%d.bmp", frame_number);

  int res =
      StoreRAWImage((unsigned char *)p, gFmt.fmt.pix.width, gFmt.fmt.pix.height,
                    gFmt.fmt.pix.pixelformat, filename);

  if (0 == res) return;

  /* BMP was not saved, uisng raw buffer */
  sprintf(filename, "frame-%d.raw", frame_number);

  FILE *fp = fopen(filename, "wb");

  if (out_buf) fwrite(p, size, 1, fp);

  fflush(fp);
  fclose(fp);
}

static int read_frame(void) {
  struct v4l2_buffer buf;
  unsigned int i;

  switch (io) {
    case IO_METHOD_READ:
      if (-1 == read(fd, buffers[0].start, buffers[0].length)) {
        switch (errno) {
          case EAGAIN:
            return 0;

          case EIO:
            /* Could ignore EIO, see spec. */

            /* fall through */

          default:
            errno_exit("read");
        }
      }

      process_image(buffers[0].start, buffers[0].length);
      break;

    case IO_METHOD_MMAP:
      CLEAR(buf);

      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;

      if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
          case EAGAIN:
            return 0;

          case EIO:
            /* Could ignore EIO, see spec. */

            /* fall through */

          default:
            errno_exit("VIDIOC_DQBUF");
        }
      }

      assert(buf.index < n_buffers);

      process_image(buffers[buf.index].start, buf.bytesused);

      if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
      break;

    case IO_METHOD_USERPTR:
      CLEAR(buf);

      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_USERPTR;

      if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
          case EAGAIN:
            return 0;

          case EIO:
            /* Could ignore EIO, see spec. */

            /* fall through */

          default:
            errno_exit("VIDIOC_DQBUF");
        }
      }

      for (i = 0; i < n_buffers; ++i)
        if (buf.m.userptr == (unsigned long)buffers[i].start &&
            buf.length == buffers[i].length)
          break;

      assert(i < n_buffers);

      process_image((void *)buf.m.userptr, buf.bytesused);

      if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
      break;
  }

  return 1;
}

static void mainloop(void) {
  unsigned int count;

  count = frame_count;

  while (count-- > 0) {
    for (;;) {
      fd_set fds;
      struct timeval tv;
      int r;

      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      /* Timeout. */
      tv.tv_sec = 2;
      tv.tv_usec = 0;

      r = select(fd + 1, &fds, NULL, NULL, &tv);

      if (-1 == r) {
        if (EINTR == errno) continue;
        errno_exit("select");
      }

      if (0 == r) {
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
      }

      if (read_frame()) break;
      /* EAGAIN - continue select loop. */
    }
  }
}

static void stop_capturing(void) {
  enum v4l2_buf_type type;

  switch (io) {
    case IO_METHOD_READ:
      /* Nothing to do. */
      break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
        errno_exit("VIDIOC_STREAMOFF");
      break;
  }
}

static void start_capturing(void) {
  unsigned int i;
  enum v4l2_buf_type type;

  switch (io) {
    case IO_METHOD_READ:
      /* Nothing to do. */
      break;

    case IO_METHOD_MMAP:
      for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
      }
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");
      break;

    case IO_METHOD_USERPTR:
      for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = (unsigned long)buffers[i].start;
        buf.length = buffers[i].length;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
      }
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");
      break;
  }
}

static void uninit_device(void) {
  unsigned int i;

  switch (io) {
    case IO_METHOD_READ:
      free(buffers[0].start);
      break;

    case IO_METHOD_MMAP:
      for (i = 0; i < n_buffers; ++i)
        if (-1 == munmap(buffers[i].start, buffers[i].length))
          errno_exit("munmap");
      break;

    case IO_METHOD_USERPTR:
      for (i = 0; i < n_buffers; ++i) free(buffers[i].start);
      break;
  }

  free(buffers);
}

static void init_read(unsigned int buffer_size) {
  buffers = calloc(1, sizeof(*buffers));

  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  buffers[0].length = buffer_size;
  buffers[0].start = malloc(buffer_size);

  if (!buffers[0].start) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
}

static void init_mmap(void) {
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr,
              "%s does not support "
              "memory mapping\n",
              dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }

  if (req.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
    exit(EXIT_FAILURE);
  }

  buffers = calloc(req.count, sizeof(*buffers));

  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
    struct v4l2_buffer buf;

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = n_buffers;

    if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) errno_exit("VIDIOC_QUERYBUF");

    buffers[n_buffers].length = buf.length;
    buffers[n_buffers].start =
        mmap(NULL /* start anywhere */, buf.length,
             PROT_READ | PROT_WRITE /* required */,
             MAP_SHARED /* recommended */, fd, buf.m.offset);

    if (MAP_FAILED == buffers[n_buffers].start) errno_exit("mmap");
  }
}

static void init_userp(unsigned int buffer_size) {
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr,
              "%s does not support "
              "user pointer i/o\n",
              dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }

  buffers = calloc(4, sizeof(*buffers));

  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
    buffers[n_buffers].length = buffer_size;
    buffers[n_buffers].start = malloc(buffer_size);

    if (!buffers[n_buffers].start) {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
  }
}

static void init_device(void) {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;

  /* Moved to global */
  /*  struct v4l2_format fmt; */

  unsigned int min;

  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n", dev_name);
    exit(EXIT_FAILURE);
  }

  switch (io) {
    case IO_METHOD_READ:
      if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
        fprintf(stderr, "%s does not support read i/o\n", dev_name);
        exit(EXIT_FAILURE);
      }
      break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
      if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
        exit(EXIT_FAILURE);
      }
      break;
  }

  /* Select video input, video standard and tune here. */

  CLEAR(cropcap);

  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */

    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
        case EINVAL:
          /* Cropping not supported. */
          break;
        default:
          /* Errors ignored. */
          break;
      }
    }
  } else {
    /* Errors ignored. */
  }

  CLEAR(gFmt);

  gFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (force_format) {
    fprintf(stderr, "Set H264\r\n");
    gFmt.fmt.pix.width = 640;
    gFmt.fmt.pix.height = 480;
    gFmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    gFmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &gFmt)) errno_exit("VIDIOC_S_FMT");

    /* Note VIDIOC_S_FMT may change width and height. */
  } else {
    /* Preserve original settings as set by v4l2-ctl for example */
    if (-1 == xioctl(fd, VIDIOC_G_FMT, &gFmt)) errno_exit("VIDIOC_G_FMT");
  }

  /* Debug print */
  char fourcc[5] = {0};
  strncpy(fourcc, (char *)&gFmt.fmt.pix.pixelformat, 4);
  printf(
      "Selected Camera Mode:\n"
      "  Width: %d\n"
      "  Height: %d\n"
      "  PixFmt: %s\n"
      "  Field: %d\n",
      gFmt.fmt.pix.width, gFmt.fmt.pix.height, fourcc, gFmt.fmt.pix.field);

  /* Buggy driver paranoia. */
  min = gFmt.fmt.pix.width * 2;
  if (gFmt.fmt.pix.bytesperline < min) gFmt.fmt.pix.bytesperline = min;
  min = gFmt.fmt.pix.bytesperline * gFmt.fmt.pix.height;
  if (gFmt.fmt.pix.sizeimage < min) gFmt.fmt.pix.sizeimage = min;

  switch (io) {
    case IO_METHOD_READ:
      init_read(gFmt.fmt.pix.sizeimage);
      break;

    case IO_METHOD_MMAP:
      init_mmap();
      break;

    case IO_METHOD_USERPTR:
      init_userp(gFmt.fmt.pix.sizeimage);
      break;
  }
}

static void close_device(void) {
  if (-1 == close(fd)) errno_exit("close");

  fd = -1;
}

static void open_device(void) {
  struct stat st;

  if (-1 == stat(dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno,
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no device\n", dev_name);
    exit(EXIT_FAILURE);
  }

  fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

  if (-1 == fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
            strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void usage(FILE *fp, int argc, char **argv) {
  fprintf(fp,
          "Usage: %s [options]\n\n"
          "Version 1.3\n"
          "Options:\n"
          "-d | --device name   Video device name [%s]\n"
          "-h | --help          Print this message\n"
          "-m | --mmap          Use memory mapped buffers [default]\n"
          "-r | --read          Use read() calls\n"
          "-u | --userp         Use application allocated buffers\n"
          "-o | --output        Outputs stream to stdout\n"
          "-f | --format        Force format to 640x480 YUYV\n"
          "-c | --count         Number of frames to grab [%i]\n"
          "",
          argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option long_options[] = {
    {"device", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {"mmap", no_argument, NULL, 'm'},
    {"read", no_argument, NULL, 'r'},
    {"userp", no_argument, NULL, 'u'},
    {"output", no_argument, NULL, 'o'},
    {"format", no_argument, NULL, 'f'},
    {"count", required_argument, NULL, 'c'},
    {0, 0, 0, 0}};

int main(int argc, char **argv) {
  dev_name = "/dev/video0";

  for (;;) {
    int idx;
    int c;

    c = getopt_long(argc, argv, short_options, long_options, &idx);

    if (-1 == c) break;

    switch (c) {
      case 0: /* getopt_long() flag */
        break;

      case 'd':
        dev_name = optarg;
        break;

      case 'h':
        usage(stdout, argc, argv);
        exit(EXIT_SUCCESS);

      case 'm':
        io = IO_METHOD_MMAP;
        break;

      case 'r':
        io = IO_METHOD_READ;
        break;

      case 'u':
        io = IO_METHOD_USERPTR;
        break;

      case 'o':
        out_buf++;
        break;

      case 'f':
        force_format++;
        break;

      case 'c':
        errno = 0;
        frame_count = strtol(optarg, NULL, 0);
        if (errno) errno_exit(optarg);
        break;

      default:
        usage(stderr, argc, argv);
        exit(EXIT_FAILURE);
    }
  }

  open_device();
  init_device();
  start_capturing();
  mainloop();
  stop_capturing();
  uninit_device();
  close_device();
  fprintf(stderr, "\n");
  return 0;
}

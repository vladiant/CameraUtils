/*
 * http://gaiger-programming.blogspot.com/2015/03/control-usb-camera-in-linux.html
 * http://blog.csdn.net/shaolyh/article/details/6583226
 * http://blog.csdn.net/zgyulongfei/article/details/7526249
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FOUR (4)
#define ALIGN_TO_FOUR(VAL) (((VAL) + FOUR - 1) & ~(FOUR - 1))

#define MAX_STR_LEN (256)
#define MAX_BUFFER_COUNT (64)

#ifndef FALSE
#define TRUE (1)
#define FALSE (0)
#endif

#define DATA_STORE_LEN (1024 * 1024 * 5)

#define DEBUG_LOG(args...) fprintf(stdout, args);

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

int GetSizeFromArg(char *arg, int *pWidth, int *pHeight) {
  char temp[MAX_STR_LEN], tempNum[MAX_STR_LEN];
  unsigned int iCross;

  strncpy(&temp[0], arg, MAX_STR_LEN);

  for (iCross = 0; iCross < strlen(&temp[0]); iCross++) {
    if ('x' == temp[iCross]) break;
  }

  if (strlen(&temp[0]) == iCross) {
    printf("error : format should be widthxheight\n");
    return -1;
  }

  if (0 == iCross) {
    printf("error : no width input \n");
    return -1;
  }

  if (strlen(&temp[0]) - 1 == iCross) {
    printf("error : no height input \n");
    return -1;
  }

  memcpy(&tempNum[0], &temp[0], iCross);
  tempNum[iCross] = 0;

  *pWidth = atoi(&tempNum[0]);

  memset(&tempNum[0], 0, MAX_STR_LEN);

  memcpy(&tempNum[0], &temp[iCross + 1], strlen(&temp[0]) - iCross - 1);
  tempNum[strlen(&temp[0]) - iCross - 1] = 0;

  *pHeight = atoi(&tempNum[0]);

  return 0;

} /*GetSizeFromArg*/

int StoreRAWImage(unsigned char *pMappingBuffer, int width, int height,
                  unsigned int fmt, char *pFileName) {
  char blankStr[MAX_STR_LEN];
  char outfileName[MAX_STR_LEN];

  if (V4L2_PIX_FMT_YUYV != fmt) return -1;

  memset(&blankStr[0], 0, MAX_STR_LEN);
  memset(&outfileName[0], 0, MAX_STR_LEN);

  if (0 != memcmp(pFileName, &blankStr[0], MAX_STR_LEN))
    snprintf(&outfileName[0], MAX_STR_LEN, "%s", pFileName);

  time_t t;
  struct tm lt;
  struct timeval tv;

  t = time(NULL);
  lt = *localtime(&t);
  gettimeofday(&tv, NULL);

  char tempStr[MAX_STR_LEN];

  snprintf(&tempStr[0], MAX_STR_LEN, "%d-%d-%d--%d-%d-%d-%d", lt.tm_year + 1900,
           lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
           (int)tv.tv_usec / 1000);

  strncat(&outfileName[0], &tempStr[0], MAX_STR_LEN);
  strncat(&outfileName[0], ".bmp", MAX_STR_LEN);

  unsigned char *pRGB24;
  pRGB24 = (unsigned char *)malloc(ALIGN_TO_FOUR(3 * width) * height);

  YUYV2RGB24(pMappingBuffer, width, height, pRGB24);

  BMPwriter(pRGB24, 24, width, height, &outfileName[0]);
  free(pRGB24);
  pRGB24 = NULL;

  return 0;
} /*StoreRAWImage*/

int StoreCompressedImage(unsigned char *pVideoBuffer, unsigned int len,
                         int *pFrameSize, int nFrame, unsigned int fmt,
                         char *pFileName) {
  char blankStr[MAX_STR_LEN];
  char outfileName[MAX_STR_LEN];

  if (FALSE == (V4L2_PIX_FMT_MJPEG == fmt || V4L2_PIX_FMT_H264 == fmt))
    return -1;

  memset(&blankStr[0], 0, MAX_STR_LEN);

  memset(&outfileName[0], 0, MAX_STR_LEN);

  if (0 != memcmp(pFileName, &blankStr[0], MAX_STR_LEN))
    snprintf(&outfileName[0], MAX_STR_LEN, "%s", pFileName);

  time_t t;
  struct tm lt;
  struct timeval tv;

  t = time(NULL);
  lt = *localtime(&t);
  gettimeofday(&tv, NULL);

  char timeStr[MAX_STR_LEN];

  snprintf(&timeStr[0], MAX_STR_LEN, "%d-%d-%d--%d-%d-%d-%d", lt.tm_year + 1900,
           lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
           (int)tv.tv_usec / 1000);

  strncat(&outfileName[0], &timeStr[0], MAX_STR_LEN);

  if (V4L2_PIX_FMT_MJPEG == fmt)
    strncat(&outfileName[0], ".jpg", MAX_STR_LEN);
  else
    strncat(&outfileName[0], ".h264", MAX_STR_LEN);

  FILE *fp;
  fp = fopen(&outfileName[0], "wb");
  if (fp < 0) {
    printf("open frame data file failed\n");
    return -1;
  } /*if */

  fwrite(pVideoBuffer, 1, len, fp);
  fclose(fp);

  printf("Capture data saved in %s\n", &outfileName[0]);

  if (NULL == pFrameSize || V4L2_PIX_FMT_MJPEG == fmt) return 0;

  /*save each frame size as a file*/

  memset(&outfileName[0], 0, MAX_STR_LEN);

  if (0 != memcmp(pFileName, &blankStr[0], MAX_STR_LEN))
    snprintf(&outfileName[0], MAX_STR_LEN, "%s", pFileName);

  strncat(&outfileName[0], &timeStr[0], MAX_STR_LEN);
  strncat(&outfileName[0], "_h264size.txt", MAX_STR_LEN);

  fp = fopen(&outfileName[0], "w");
  if (fp < 0) {
    printf("open frame size file failed\n");
    return -1;
  } /*if */

  int i;
  for (i = 0; i < nFrame; i++) fprintf(fp, "%d\n", pFrameSize[i]);

  fclose(fp);
  printf("frame size saved in %s\n", &outfileName[0]);

  return 0;
} /*StoreCompressedImage*/

int PrintCameraInfo(int fd) {
  if (0 > fd) return -1;

  int ret;

  struct v4l2_capability cap;
  ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);

  if (ret < 0) {
    printf("VIDIOC_QUERYCAP failed : %s\n", strerror(errno));
    return ret;
  }

  // Print capability infomations
  printf("Capability Informations:\n");
  printf("\tdriver: %s\n", cap.driver);
  printf("\tcard: %s\n", cap.card);
  printf("\tbus_info: %s\n", cap.bus_info);
  printf("\tversion:  %u.%u.%u\n", (cap.version >> 16) & 0xFF,
         (cap.version >> 8) & 0xFF, cap.version & 0xFF);
  printf("\tcapabilities: %08x\n", cap.capabilities);

  // print some capabilities, which are in most camera
  if (V4L2_CAP_VIDEO_CAPTURE & cap.capabilities)
    printf("\t\tV4L2_CAP_VIDEO_CAPTURE\n");

  if (V4L2_CAP_STREAMING & cap.capabilities) printf("\t\tV4L2_CAP_STREAMING\n");

  if (V4L2_CAP_DEVICE_CAPS & cap.capabilities)
    printf("\t\tV4L2_CAP_DEVICE_CAPS\n");

  struct v4l2_fmtdesc supportedFmt;

  supportedFmt.index = 0;
  supportedFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  printf("support resolutions:\n");
  while (0 <= ioctl(fd, VIDIOC_ENUM_FMT, &supportedFmt)) {
    printf("%s", &supportedFmt.description[0]);

    if (V4L2_FMT_FLAG_COMPRESSED & supportedFmt.flags)
      printf("\tV4L2_FMT_FLAG_COMPRESSED");

    if (V4L2_FMT_FLAG_EMULATED & supportedFmt.flags)
      printf("\tV4L2_FMT_FLAG_EMULATED");

    printf("\n");

    struct v4l2_frmsizeenum frameSize;

    frameSize.index = 0;
    frameSize.pixel_format = supportedFmt.pixelformat;

    while (0 <= ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frameSize)) {
      if (V4L2_FRMSIZE_TYPE_DISCRETE == frameSize.type) {
        printf("\t%dx%d, V4L2_FRMSIZE_TYPE_DISCRETE\n",
               frameSize.discrete.width, frameSize.discrete.height);
      } else if (V4L2_FRMSIZE_TYPE_STEPWISE == frameSize.type) {
        printf("\t%dx%d, V4L2_FRMSIZE_TYPE_STEPWISE\n",
               frameSize.stepwise.max_width, frameSize.stepwise.max_height);
      } /*if V4L2_FRMSIZE_TYPE_DISCRETE == frameSize.type*/

      frameSize.index++;
    } /*while ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0*/

    supportedFmt.index++;
  } /*while ioctl(fd, VIDIOC_ENUM_FMT, &supportedFmt) */

  return 0;
} /*PrintCameraInfo*/

void SignalInterruptHandler(int s) {
  printf("Caught signal %d : %s\n", s, strsignal(s));
  exit(s);
} /*SignalInterruptHandler*/

int main(int argc, char *argv[]) {
  int fd;
  int ret;

  int i;

  int isInquiryDeviceInfo;
  char deviceName[MAX_STR_LEN];
  char outputFileName[MAX_STR_LEN];
  int preferWidth, preferHeight;
  unsigned int preferFormat;

  /*avoiding ctrl + c event causes
   *  camera could not VIDIOC_STREAMON again.
   */

  signal(SIGINT, SignalInterruptHandler);

  isInquiryDeviceInfo = FALSE;
  memset(&deviceName[0], 0, MAX_STR_LEN);
  snprintf(&deviceName[0], MAX_STR_LEN, "/dev/video0");

  memset(&outputFileName[0], 0, MAX_STR_LEN);
  preferWidth = 640;
  preferHeight = 480;
  preferFormat = V4L2_PIX_FMT_MJPEG;

  i = 1;

  while (argc > i) {
    if (0 == strncmp(argv[i], "-d", MAX_STR_LEN)) {
      if (i + 1 < argc) {
        snprintf(&deviceName[0], MAX_STR_LEN, "%s", argv[i + 1]);
      } else {
        printf("error : -d should be followed a device!\n");
        return -1;
      } /*if */
    }   /*if 0 == strncmp(argv[i], "-d", MAX_STR_LEN)*/

    if (0 == strncmp(argv[i], "-q", MAX_STR_LEN)) isInquiryDeviceInfo = TRUE;

    if (0 == strncmp(argv[i], "-o", MAX_STR_LEN)) {
      if (i + 1 < argc) {
        snprintf(&outputFileName[0], MAX_STR_LEN, "%s", argv[i + 1]);
      } else {
        printf("error : -o should be followed output file name!\n");
        return -1;
      } /*if */
    }   /*0 == strncmp(argv[i], "-o", MAX_STR_LEN)*/

    if (0 == strncmp(argv[i], "-f", MAX_STR_LEN)) {
      if (i + 1 < argc) {
        if (0 == strncmp(argv[i + 1], "yuyv", MAX_STR_LEN)) {
          preferFormat = V4L2_PIX_FMT_YUYV;
        } else if (0 == strncmp(argv[i + 1], "jpeg", MAX_STR_LEN)) {
          preferFormat = V4L2_PIX_FMT_MJPEG;
        } else if (0 == strncmp(argv[i + 1], "h264", MAX_STR_LEN)) {
          preferFormat = V4L2_PIX_FMT_H264;
        } else {
          printf("-f support yuyv, jpeg or h264 only !\n");
          return -1;
        } /*if strncmp(argv[i + 1], "yuyv", MAX_STR_LEN)*/
      } else {
        printf("error : -f should be followed yuyv or mjpg!\n");
        return -1;
      } /*if */
    }   /*0 == strncmp(argv[k], "-o", MAX_STR_LEN)*/

    if (0 == strncmp(argv[i], "-s", MAX_STR_LEN)) {
      if (argc > i + 1) {
        int ret;
        ret = GetSizeFromArg(argv[i + 1], &preferWidth, &preferHeight);
        if (ret < 0) return -1;
      } else {
        printf("error: -s need be assigned widthxheight\n");
        return -1;
      }

    } /*if 0 == strncmp(argv[i], "-s", MAX_STR_LEN) */

    i++;
  } /*while i*/

  fd = open(&deviceName[0], O_RDWR);

  if (0 > fd) {
    perror(&deviceName[0]);
    exit(-1);
  } /*if*/

  if (FALSE != isInquiryDeviceInfo) {
    PrintCameraInfo(fd);
    close(fd);
    return -1;
  } /*if */

  struct v4l2_format fmt;

  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = preferWidth;
  fmt.fmt.pix.height = preferHeight;
  fmt.fmt.pix.pixelformat = preferFormat;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

  ret = ioctl(fd, VIDIOC_S_FMT, &fmt);

  if (0 < ret) printf("VIDIOC_S_FMT setting fail :%s\n", strerror(errno));

  /*get format*/
  ret = ioctl(fd, VIDIOC_G_FMT, &fmt);

  if (fmt.fmt.pix.width != preferWidth || fmt.fmt.pix.height != preferHeight) {
    DEBUG_LOG("warning: prefer size %dx%d could not be reached\n", preferWidth,
              preferHeight);
  } /*if */

  if (preferFormat != fmt.fmt.pix.pixelformat) {
    char preferPixelFmtStr[8];
    memset(&preferPixelFmtStr[0], 0, 8);
    memcpy(&preferPixelFmtStr[0], &preferFormat, 4);

    DEBUG_LOG("warning: prefer format %s could not be reached\n",
              &preferPixelFmtStr[0]);
  }
  // Print Stream Format

  printf("Stream Format Informations:\n");
  printf("\ttype: %d\n", fmt.type);
  printf("\twidth: %d\n", fmt.fmt.pix.width);
  printf("\theight: %d\n", fmt.fmt.pix.height);

#if (0)
  struct v4l2_jpegcompression jpegCompression;

  memset(&jpegCompression, 0, sizeof(struct v4l2_jpegcompression));

  ret = ioctl(fd, VIDIOC_G_JPEGCOMP, &jpegCompression);

  // printf("__LINE__ = %d\n", __LINE__);
  if (0 < ret) {
    printf("VIDIOC_G_JPEGCOMP not supported\n");
  } else {
    jpegCompression.quality = 1;

    ioctl(fd, VIDIOC_S_JPEGCOMP, &jpegCompression);
    printf("jpeg quality = %d\n", jpegCompression.quality);
  } /*if */
#endif

  char pixelFmtStr[8];
  memset(&pixelFmtStr[0], 0, 8);
  memcpy(&pixelFmtStr[0], &fmt.fmt.pix.pixelformat, 4);

  printf("\tpixelformat: %s\n", &pixelFmtStr[0]);
  printf("\tfield: %d\n", fmt.fmt.pix.field);
  printf("\tbytesperline: %d\n", fmt.fmt.pix.bytesperline);
  printf("\tsizeimage: %d\n", fmt.fmt.pix.sizeimage);
  printf("\tcolorspace: %d\n", fmt.fmt.pix.colorspace);
  printf("\tpriv: %d\n", fmt.fmt.pix.priv);
  printf("\traw_data: %p\n", fmt.fmt.raw_data);

  /*
   * request buffers
   */

  struct v4l2_requestbuffers reqbuf;

  reqbuf.count = MAX_BUFFER_COUNT;
  reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  reqbuf.memory = V4L2_MEMORY_MMAP;

  ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
  if (ret < 0) {
    printf("VIDIOC_REQBUFS failed : %s\n", strerror(errno));
    return ret;
  } /*if*/

  /*after calling ioctl(fd, VIDIOC_REQBUFS, &reqbuf), the reqbuf.count maybe
   * modified*/
  printf("reqbuf.count = %d\n", reqbuf.count);

  /*
   * map the buffers
   */

  char *mappingBuffer[MAX_BUFFER_COUNT];
  unsigned int mappingBufferLength[MAX_BUFFER_COUNT];

  struct v4l2_buffer v4l2_buf;

  for (i = 0; i < reqbuf.count; i++) {
    memset(&v4l2_buf, 0, sizeof(struct v4l2_buffer));

    v4l2_buf.index = i;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fd, VIDIOC_QUERYBUF, &v4l2_buf);

    if (ret < 0) {
      printf("VIDIOC_QUERYBUF %d, failed : %s\n", i, strerror(errno));
      return ret;
    } /*if*/

    /* map buffer */
    mappingBuffer[i] = (char *)mmap(0, v4l2_buf.length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, v4l2_buf.m.offset);

    mappingBufferLength[i] = v4l2_buf.length;

    if (MAP_FAILED == mappingBuffer[i]) {
      printf("mmap (%d) failed : %s\n", i, strerror(errno));
      return -1;
    } /*if */

    /*
     * Enqueue buffer
     */
    ret = ioctl(fd, VIDIOC_QBUF, &v4l2_buf);

    if (ret < 0) {
      printf("VIDIOC_QBUF (%d) failed : %s \n", i, strerror(errno));
      return -1;
    } /*if*/
#if (0)
#ifdef __LP64__
    printf("Frame buffer %d: address=%#llx, length=%u\n", i,
           (unsigned long long)mappingBuffer[i], v4l2_buf.length);
#else
    printf("Frame buffer %d: address=%#x, length=%u\n", i,
           (unsigned int)mappingBuffer[i], v4l2_buf.length);
#endif
#endif
  } /*for i*/

  /*
   *   start recording
   */
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  ret = ioctl(fd, VIDIOC_STREAMON, &type);
  if (0 > ret) {
    printf("VIDIOC_STREAMON failed : %s\n", strerror(errno));
    return ret;
  } /*if*/

  int count;
  count = 5;

  if (V4L2_PIX_FMT_H264 == fmt.fmt.pix.pixelformat) count = 250;

  unsigned int storedH264DataLen;

  unsigned char h264Data[DATA_STORE_LEN];

  memset(&h264Data[0], 0, DATA_STORE_LEN);
  storedH264DataLen = 0;

  int frameSize[4096];

  memset(&frameSize[0], 0, 4096 * sizeof(int));

  i = 0;

  int k;
  k = 0;

  while (k < count) {
    /*
     *  Get frame
     */
    ret = ioctl(fd, VIDIOC_DQBUF, &v4l2_buf);

    if (0 > ret) {
      printf("VIDIOC_DQBUF failed : %s\n", strerror(errno));
      return ret;
    } /*if */

    printf("captured image size = %3.2f KB\n", v4l2_buf.bytesused / 1024.0);

    // Debug print
    char fourcc[5] = {0};
    strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
    printf("Selected Camera PixFmt: %s\n", fourcc);

    if (V4L2_PIX_FMT_H264 == fmt.fmt.pix.pixelformat) {
      memcpy(&h264Data[storedH264DataLen], mappingBuffer[v4l2_buf.index],
             v4l2_buf.bytesused);

      frameSize[i] = v4l2_buf.bytesused;
      storedH264DataLen += v4l2_buf.bytesused;
      i++;

      if ((DATA_STORE_LEN -
           storedH264DataLen) /*the jpeg max size ~ 20% of BMP*/
              < (fmt.fmt.pix.width * fmt.fmt.pix.height) / 5 ||
          0 == count) {
        StoreCompressedImage(&h264Data[0], storedH264DataLen, &frameSize[0], i,
                             fmt.fmt.pix.pixelformat, &outputFileName[0]);

        memset(&h264Data[0], 0, DATA_STORE_LEN);
        storedH264DataLen = 0;
        i = 0;

      } /*if */
    } else if (V4L2_PIX_FMT_MJPEG == fmt.fmt.pix.pixelformat) {
      StoreCompressedImage((unsigned char *)mappingBuffer[v4l2_buf.index],
                           v4l2_buf.bytesused, NULL, 0, fmt.fmt.pix.pixelformat,
                           &outputFileName[0]);
    } else {
      StoreRAWImage((unsigned char *)mappingBuffer[v4l2_buf.index],
                    fmt.fmt.pix.width, fmt.fmt.pix.height,
                    fmt.fmt.pix.pixelformat, &outputFileName[0]);
    } /*if V4L2_PIX_FMT_H264 == fmt.fmt.pix.pixelformat*/

    // Re-queue buffer
    ret = ioctl(fd, VIDIOC_QBUF, &v4l2_buf);
    if (ret < 0) {
      printf("VIDIOC_QBUF failed : %s\n", strerror(errno));
      return ret;
    }

    if (V4L2_PIX_FMT_H264 != fmt.fmt.pix.pixelformat) usleep(200 * 1000);

    k++;
  } /*while*/

  if (V4L2_PIX_FMT_H264 == fmt.fmt.pix.pixelformat) {
    StoreCompressedImage(&h264Data[0], storedH264DataLen, &frameSize[0], i,
                         fmt.fmt.pix.pixelformat, &outputFileName[0]);
  }

  /*
   *  Stop capture
   */

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
  if (ret < 0) {
    printf("VIDIOC_QBUF failed : %s\n", strerror(errno));
    return ret;
  } /*if */

  /*
   *  Release the resources
   */
  for (i = 0; i < MAX_BUFFER_COUNT; i++)
    munmap(mappingBuffer[i], mappingBufferLength[i]);

  close(fd);
} /*main*/

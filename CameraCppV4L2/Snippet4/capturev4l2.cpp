// https://jayrambhia.com/blog/capture-v4l2

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>

uint8_t* gBuffer = nullptr;
struct v4l2_format gFmt = {0};

static int xioctl(int fd, int request, void* arg) {
  int r;

  do
    r = ioctl(fd, request, arg);
  while (-1 == r && EINTR == errno);

  return r;
}

int print_caps(int fd) {
  struct v4l2_capability caps = {};
  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps)) {
    perror("Querying Capabilities");
    return 1;
  }

  printf(
      "Driver Caps:\n"
      "  Driver: \"%s\"\n"
      "  Card: \"%s\"\n"
      "  Bus: \"%s\"\n"
      "  Version: %d.%d\n"
      "  Capabilities: %08x\n",
      caps.driver, caps.card, caps.bus_info, (caps.version >> 16) && 0xff,
      (caps.version >> 24) && 0xff, caps.capabilities);

  struct v4l2_cropcap cropcap = {0};
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    perror("Querying Cropping Capabilities");
    return 1;
  }

  printf(
      "Camera Cropping:\n"
      "  Bounds: %dx%d+%d+%d\n"
      "  Default: %dx%d+%d+%d\n"
      "  Aspect: %d/%d\n",
      cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left,
      cropcap.bounds.top, cropcap.defrect.width, cropcap.defrect.height,
      cropcap.defrect.left, cropcap.defrect.top, cropcap.pixelaspect.numerator,
      cropcap.pixelaspect.denominator);

  int support_grbg10 = 0;

  struct v4l2_fmtdesc fmtdesc = {0};
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  char fourcc[5] = {0};
  char c, e;
  printf("  FMT : CE Desc\n--------------------\n");
  while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
    strncpy(fourcc, (char*)&fmtdesc.pixelformat, 4);
    if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10) support_grbg10 = 1;
    c = fmtdesc.flags & 1 ? 'C' : ' ';
    e = fmtdesc.flags & 2 ? 'E' : ' ';
    printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
    fmtdesc.index++;
  }
  /*
  if (!support_grbg10)
  {
      printf("Doesn't support GRBG10.\n");
      return 1;
  }*/

  gFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  gFmt.fmt.pix.width = 640;
  gFmt.fmt.pix.height = 480;
  // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
  // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
  gFmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  gFmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (-1 == xioctl(fd, VIDIOC_S_FMT, &gFmt)) {
    perror("Setting Pixel Format");
    return 1;
  }

  strncpy(fourcc, (char*)&gFmt.fmt.pix.pixelformat, 4);
  printf(
      "Selected Camera Mode:\n"
      "  Width: %d\n"
      "  Height: %d\n"
      "  PixFmt: %s\n"
      "  Field: %d\n",
      gFmt.fmt.pix.width, gFmt.fmt.pix.height, fourcc, gFmt.fmt.pix.field);
  return 0;
}

int init_mmap(int fd) {
  struct v4l2_requestbuffers req = {0};
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    perror("Requesting Buffer");
    return 1;
  }

  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;
  if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
    perror("Querying Buffer");
    return 1;
  }

  gBuffer =
      static_cast<uint8_t*>(mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, buf.m.offset));
  printf("Length: %d\nAddress: %p\n", buf.length, gBuffer);
  printf("Image Length: %d\n", buf.bytesused);

  return 0;
}

int capture_image(int fd) {
  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;
  if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
    perror("Query Buffer");
    return 1;
  }

  if (-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type)) {
    perror("Start Capture");
    return 1;
  }

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval tv = {0};
  tv.tv_sec = 2;
  int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
  if (-1 == r) {
    perror("Waiting for Frame");
    return 1;
  }

  if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
    perror("Retrieving Frame");
    return 1;
  }
  printf("saving image\n");

  if (V4L2_PIX_FMT_MJPEG == gFmt.fmt.pix.pixelformat) {
    cv::Mat cvmat(gFmt.fmt.pix.height, gFmt.fmt.pix.width, CV_8UC3,
                  (void*)gBuffer);
    cv::Mat frame = cv::imdecode(cvmat, cv::IMREAD_COLOR);
    cv::namedWindow("window", cv::WINDOW_AUTOSIZE);
    cv::imshow("window", frame);
    cv::imwrite("image.jpg", frame);

  } else if (V4L2_PIX_FMT_YUYV == gFmt.fmt.pix.pixelformat) {
    cv::Mat cvmat1(gFmt.fmt.pix.height, gFmt.fmt.pix.width, CV_8UC2,
                   (void*)gBuffer);
    cv::Mat cvmat(gFmt.fmt.pix.height, gFmt.fmt.pix.width, CV_8UC3);
    cv::cvtColor(cvmat1, cvmat, cv::COLOR_YUV2RGB_YVYU);

    cv::namedWindow("window", cv::WINDOW_AUTOSIZE);
    cv::imshow("window", cvmat);
    cv::imwrite("image.bmp", cvmat);
  }

  printf("press any key to continue...\n");
  cv::waitKey(0);

  return 0;
}

int main() {
  int fd;

  fd = open("/dev/video0", O_RDWR);
  if (fd == -1) {
    perror("Opening video device");
    return 1;
  }
  if (print_caps(fd)) return 1;

  if (init_mmap(fd)) return 1;
  int i;
  for (i = 0; i < 5; i++) {
    if (capture_image(fd)) return 1;
  }
  close(fd);

  printf("Done.\n");

  return 0;
}

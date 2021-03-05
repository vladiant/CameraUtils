#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

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

using namespace std;

int main() {
  // 1.  Open the device
  int fd;  // A file descriptor to the video device
  fd = open("/dev/video0", O_RDWR);
  if (fd < 0) {
    perror("Failed to open device, OPEN");
    return 1;
  }

  // 2. Ask the device if it can capture frames
  v4l2_capability capability;
  if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
    // something went wrong... exit
    perror("Failed to get device capabilities, VIDIOC_QUERYCAP");
    return 1;
  }

  // 3. Set Image format
  v4l2_format imageFormat;
  imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  imageFormat.fmt.pix.width = 1024;
  imageFormat.fmt.pix.height = 1024;
  imageFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  imageFormat.fmt.pix.field = V4L2_FIELD_NONE;

  // Debug print
  char initial_fourcc[5] = {0};
  strncpy(initial_fourcc, (char *)&imageFormat.fmt.pix.pixelformat, 4);
  printf(
      "Requested Camera Mode:\n"
      "  Width: %d\n"
      "  Height: %d\n"
      "  PixFmt: %s\n"
      "  Field: %d\n",
      imageFormat.fmt.pix.width, imageFormat.fmt.pix.height, initial_fourcc,
      imageFormat.fmt.pix.field);

  // tell the device you are using this format
  if (ioctl(fd, VIDIOC_S_FMT, &imageFormat) < 0) {
    perror("Device could not set format, VIDIOC_S_FMT");
    return 1;
  }

  // Debug print
  char fourcc[5] = {0};
  strncpy(fourcc, (char *)&imageFormat.fmt.pix.pixelformat, 4);
  printf(
      "Selected Camera Mode:\n"
      "  Width: %d\n"
      "  Height: %d\n"
      "  PixFmt: %s\n"
      "  Field: %d\n",
      imageFormat.fmt.pix.width, imageFormat.fmt.pix.height, fourcc,
      imageFormat.fmt.pix.field);

  // 4. Request Buffers from the device
  v4l2_requestbuffers requestBuffer = {0};
  requestBuffer.count = 1;  // one request buffer
  requestBuffer.type =
      V4L2_BUF_TYPE_VIDEO_CAPTURE;  // request a buffer wich we an use for
                                    // capturing frames
  requestBuffer.memory = V4L2_MEMORY_MMAP;

  if (ioctl(fd, VIDIOC_REQBUFS, &requestBuffer) < 0) {
    perror("Could not request buffer from device, VIDIOC_REQBUFS");
    return 1;
  }

  // 5. Quety the buffer to get raw data ie. ask for the you requested buffer
  // and allocate memory for it
  v4l2_buffer queryBuffer = {0};
  queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  queryBuffer.memory = V4L2_MEMORY_MMAP;
  queryBuffer.index = 0;
  if (ioctl(fd, VIDIOC_QUERYBUF, &queryBuffer) < 0) {
    perror("Device did not return the buffer information, VIDIOC_QUERYBUF");
    return 1;
  }
  // use a pointer to point to the newly created buffer
  // mmap() will map the memory address of the device to
  // an address in memory
  char *buffer = (char *)mmap(NULL, queryBuffer.length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, queryBuffer.m.offset);
  memset(buffer, 0, queryBuffer.length);

  // 6. Get a frame
  // Create a new buffer type so the device knows whichbuffer we are talking
  // about
  v4l2_buffer bufferinfo;
  memset(&bufferinfo, 0, sizeof(bufferinfo));
  bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  bufferinfo.memory = V4L2_MEMORY_MMAP;
  bufferinfo.index = 0;

  // Activate streaming
  int type = bufferinfo.type;
  if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
    perror("Could not start streaming, VIDIOC_STREAMON");
    return 1;
  }

  /***************************** Begin looping here *********************/
  // Queue the buffer
  if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
    perror("Could not queue buffer, VIDIOC_QBUF");
    return 1;
  }

  // Dequeue the buffer
  if (ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0) {
    perror("Could not dequeue the buffer, VIDIOC_DQBUF");
    return 1;
  }
  // Frames get written after dequeuing the buffer

  cout << "Buffer has: " << (double)bufferinfo.bytesused / 1024
       << " KBytes of data" << endl;

  // Write the data out to file
  if (V4L2_PIX_FMT_MJPEG == imageFormat.fmt.pix.pixelformat) {
    ofstream outFile;
    outFile.open("webcam_output.jpeg", ios::binary | ios::app);

    int bufPos = 0,
        outFileMemBlockSize = 0;  // the position in the buffer and the amoun to
                                  // copy from the buffer
    int remainingBufferSize =
        bufferinfo.bytesused;  // the remaining buffer size, is decremented by
                               // memBlockSize amount on each loop so we do not
                               // overwrite the buffer
    char *outFileMemBlock = NULL;  // a pointer to a new memory block
    int itr = 0;                   // counts thenumber of iterations
    while (remainingBufferSize > 0) {
      bufPos +=
          outFileMemBlockSize;  // increment the buffer pointer on each loop
                                // initialise bufPos before outFileMemBlockSize
                                // so we can start at the begining of the buffer

      outFileMemBlockSize =
          1024;  // set the output block size to a preferable size. 1024 :)
      outFileMemBlock = new char[sizeof(char) * outFileMemBlockSize];

      // copy 1024 bytes of data starting from buffer+bufPos
      memcpy(outFileMemBlock, buffer + bufPos, outFileMemBlockSize);
      outFile.write(outFileMemBlock, outFileMemBlockSize);

      // calculate the amount of memory left to read
      // if the memory block size is greater than the remaining
      // amount of data we have to copy
      if (outFileMemBlockSize > remainingBufferSize)
        outFileMemBlockSize = remainingBufferSize;

      // subtract the amount of data we have to copy
      // from the remaining buffer size
      remainingBufferSize -= outFileMemBlockSize;

      // display the remaining buffer size
      cout << itr++ << " Remaining bytes: " << remainingBufferSize << endl;

      delete outFileMemBlock;
    }

    // Close the file
    outFile.close();
  } else if (V4L2_PIX_FMT_YUYV == imageFormat.fmt.pix.pixelformat) {
    unsigned char *pRGB24;
    pRGB24 =
        (unsigned char *)malloc(ALIGN_TO_FOUR(3 * imageFormat.fmt.pix.width) *
                                imageFormat.fmt.pix.height);

    YUYV2RGB24((unsigned char *)buffer, imageFormat.fmt.pix.width,
               imageFormat.fmt.pix.height, pRGB24);

    char filename[] = "webcam_output.bmp";

    BMPwriter(pRGB24, 24, imageFormat.fmt.pix.width, imageFormat.fmt.pix.height,
              filename);
    free(pRGB24);
    pRGB24 = NULL;
  }

  /******************************** end looping here **********************/

  // end streaming
  if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
    perror("Could not end streaming, VIDIOC_STREAMOFF");
    return 1;
  }

  close(fd);
  return 0;
}

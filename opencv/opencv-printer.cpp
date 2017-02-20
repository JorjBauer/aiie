#include "opencv-printer.h"

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/features2d/features2d.hpp"

using namespace cv;
using namespace std;

#define WINDOWNAME "printer"

#define HEIGHT 800
#define NATIVEWIDTH 960 // FIXME: printer can change density...


//#define WIDTH 384 // emulating the teeny printer I've got
#define WIDTH 960

OpenCVPrinter::OpenCVPrinter()
{
  pixels = new Mat(HEIGHT, WIDTH, CV_8U);
  *pixels = cv::Scalar(0xFF);
  ypos = 0;
  isDirty = false;
  namedWindow(WINDOWNAME, CV_WINDOW_AUTOSIZE);
}

OpenCVPrinter::~OpenCVPrinter()
{
  delete pixels; pixels = NULL;
}

void OpenCVPrinter::update()
{
  if (isDirty) {
    imshow(WINDOWNAME, *pixels);
    isDirty = false;
  }
}

void OpenCVPrinter::addLine(uint8_t *rowOfBits)
{
  isDirty = true;
  for (int yoff=0; yoff<8; yoff++) {
    // 960 pixels == 120 bytes -- FIXME
    for (int i=0; i<(NATIVEWIDTH/8); i++) {
      uint8_t bv = rowOfBits[yoff*120+i];
      for (int xoff=0; xoff<8; xoff++) {
	// scale X from "actual FX80" coordinates to "real printer" coordinates
	uint16_t actualX = (uint16_t)(((float)(i*8+xoff) * (float)WIDTH) / (float)NATIVEWIDTH);

	uint8_t oldPixel = pixels->at<uchar>((ypos + yoff)%HEIGHT, actualX);
	uint8_t pixelColor = (bv & (1 << (7-xoff))) ? 0x00 : 0xFF;
	// Make sure to preserve any pixels we've already drawn
	pixels->at<uchar>((ypos + yoff)%HEIGHT, actualX) = ~(~oldPixel | ~pixelColor);
      }
    }
  }

  if (ypos >= HEIGHT) {
    ypos = 0;
  }
}

void OpenCVPrinter::moveDownPixels(uint8_t p)
{
  ypos+= p;
  if (ypos >= HEIGHT) {
    // clear page & restart
    *pixels = cv::Scalar(0xFF);
    ypos = 0;
  }
}

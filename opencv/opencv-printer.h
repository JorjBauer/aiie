#ifndef __OPENCV_PRINTER_H
#define __OPENCV_PRINTER_H

#include <stdlib.h>

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/features2d/features2d.hpp"

#include "physicalprinter.h"

class OpenCVPrinter : public PhysicalPrinter {
 public:
  OpenCVPrinter();
  virtual ~OpenCVPrinter();

  virtual void addLine(uint8_t *rowOfBits); // must be 960 pixels wide (120 bytes)

  virtual void update();

  virtual void moveDownPixels(uint8_t p);

 private:
  bool isDirty;
  uint16_t ypos;
  cv::Mat *pixels;
};

#endif

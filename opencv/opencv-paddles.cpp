#include <stdio.h>

#include "opencv-paddles.h"

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/features2d/features2d.hpp"

using namespace cv;
using namespace std;

// FIXME: abstract this somewhere
#define WINDOWNAME "6502core"
#define WINDOWHEIGHT (240*2)
#define WINDOWWIDTH (320*2)

#include "globals.h"

static void mouseCallback(int event, int x, int y, int flags, void* userdata)
{
  OpenCVPaddles *a = (OpenCVPaddles *)userdata;

  if (event == EVENT_MOUSEMOVE) {
    a->p0 = ((float) x / (float)WINDOWWIDTH) * 255.0;
    a->p1 = ((float) y / (float)WINDOWHEIGHT) * 255.0;
  }
}

OpenCVPaddles::OpenCVPaddles()
{
  p0 = p1 = 127;
  setMouseCallback(WINDOWNAME, mouseCallback, this);
}

OpenCVPaddles::~OpenCVPaddles()
{
}

void OpenCVPaddles::startReading()
{
  g_vm->triggerPaddleInCycles(0, 12 * p0);
  g_vm->triggerPaddleInCycles(1, 12 * p1);
}

uint8_t OpenCVPaddles::paddle0()
{
  return p0;
}

uint8_t OpenCVPaddles::paddle1()
{
  return p1;
}

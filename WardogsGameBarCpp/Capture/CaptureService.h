#pragma once
#include "../OCR/CoordinateRecognizer.h"
namespace wardogs {
Image CaptureAroundCursor(int size);
// Fixed desktop rectangle; used only while an explicitly requested scan is active.
Image CaptureRegion(int left,int top,int width,int height);
}

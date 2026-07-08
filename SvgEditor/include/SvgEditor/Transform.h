#pragma once

#include "SelectionTool.h"
#include "SvgDocument.h"

namespace SvgEditor {

class Transform {
 public:
  enum class Handle {
    None,
    Delete,
    Rotate,
    Duplicate,
    Scale
  };

  struct HandleRects {
    BLRect deleteRect;
    BLRect rotateRect;
    BLRect duplicateRect;
    BLRect scaleRect;
  };

  static HandleRects handleRects(const BLRect& screenBounds);
  static Handle hitTestHandle(const BLRect& screenBounds, const BLPoint& screenPoint);
  static BLMatrix2D makeTranslation(double dx, double dy);
  static BLMatrix2D makeRotation(double radians, const BLPoint& anchor);
  static BLMatrix2D makeScale(const BLPoint& anchor, double sx, double sy);
};

}  // namespace SvgEditor

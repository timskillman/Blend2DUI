#include "SvgEditor/Transform.h"

#include "Utility.h"

namespace SvgEditor {
namespace {

BLRect centeredHandle(const BLPoint& center, double size) {
  return BLRect(center.x - size * 0.5, center.y - size * 0.5, size, size);
}

}  // namespace

Transform::HandleRects Transform::handleRects(const BLRect& screenBounds) {
  constexpr double kHandleSize = 18.0;
  return HandleRects{
      centeredHandle(BLPoint(screenBounds.x, screenBounds.y), kHandleSize),
      centeredHandle(BLPoint(screenBounds.x + screenBounds.w, screenBounds.y), kHandleSize),
      centeredHandle(BLPoint(screenBounds.x, screenBounds.y + screenBounds.h), kHandleSize),
      centeredHandle(BLPoint(screenBounds.x + screenBounds.w, screenBounds.y + screenBounds.h), kHandleSize),
  };
}

Transform::Handle Transform::hitTestHandle(const BLRect& screenBounds, const BLPoint& screenPoint) {
  const HandleRects handles = handleRects(screenBounds);
  if (Blend2DUI::contains(handles.deleteRect, screenPoint.x, screenPoint.y)) return Handle::Delete;
  if (Blend2DUI::contains(handles.rotateRect, screenPoint.x, screenPoint.y)) return Handle::Rotate;
  if (Blend2DUI::contains(handles.duplicateRect, screenPoint.x, screenPoint.y)) return Handle::Duplicate;
  if (Blend2DUI::contains(handles.scaleRect, screenPoint.x, screenPoint.y)) return Handle::Scale;
  return Handle::None;
}

BLMatrix2D Transform::makeTranslation(double dx, double dy) {
  return BLMatrix2D::make_translation(dx, dy);
}

BLMatrix2D Transform::makeRotation(double radians, const BLPoint& anchor) {
  BLMatrix2D matrix = BLMatrix2D::make_identity();
  matrix.post_translate(-anchor.x, -anchor.y);
  matrix.post_rotate(radians);
  matrix.post_translate(anchor.x, anchor.y);
  return matrix;
}

BLMatrix2D Transform::makeScale(const BLPoint& anchor, double sx, double sy) {
  BLMatrix2D matrix = BLMatrix2D::make_identity();
  matrix.post_translate(-anchor.x, -anchor.y);
  matrix.post_scale(sx, sy);
  matrix.post_translate(anchor.x, anchor.y);
  return matrix;
}

}  // namespace SvgEditor

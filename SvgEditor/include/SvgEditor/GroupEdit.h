#pragma once

#include "SelectionTool.h"
#include "SvgDocument.h"

namespace SvgEditor {

class GroupEdit {
 public:
  static bool groupSelection(SvgDocument& document, SelectionTool& selection);
  static bool ungroupSelection(SvgDocument& document, SelectionTool& selection);
};

}  // namespace SvgEditor

#pragma once

#include "SvgDocument.h"

#include <string>

namespace SvgEditor {

bool loadSvgDocument(const std::string& path, SvgDocument& document);
bool saveSvgDocument(const SvgDocument& document, const std::string& path);

}  // namespace SvgEditor

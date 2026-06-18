CXX ?= g++
PKG_CONFIG ?= pkg-config

TARGET := blend2d_shapes_demo
SOURCES := SvgRender/src/main.cpp
SVG_SOURCES := SvgRender/src/SvgRenderer.cpp

CPPFLAGS ?= -ISvgRender/include
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS ?=
LDLIBS ?= -lblend2d

ifneq ($(wildcard build/third_party/blend2d/libblend2d.a),)
  CPPFLAGS += -DBL_STATIC -I../third_party/blend2d
  LDLIBS := build/third_party/blend2d/libblend2d.a -lm
endif

ifneq ($(shell $(PKG_CONFIG) --exists blend2d && echo yes),)
  CPPFLAGS += $(shell $(PKG_CONFIG) --cflags blend2d)
  LDLIBS := $(shell $(PKG_CONFIG) --libs blend2d)
endif

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES) $(SVG_SOURCES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(SVG_SOURCES) -o $@ $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) blend2d_shapes_demo.png

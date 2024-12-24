# tangram-es external dependencies

MAKE_BASE := $(GET_MAKE_BASE)

## header-only libraries
INC += \
  $(MAKE_BASE) \
  $(MAKE_BASE)/earcut/include \
  $(MAKE_BASE)/isect2d/include \
  $(MAKE_BASE)/mapbox \
  $(MAKE_BASE)/pbf \
  $(MAKE_BASE)/stb \
  $(MAKE_BASE)/variant/include

#hash-library rapidjson


## css-color-parser-cpp
MODULE_BASE = $(MAKE_BASE)/css-color-parser-cpp

MODULE_SOURCES = csscolorparser.cpp
MODULE_INC_PUBLIC = .
#MODULE_CFLAGS = -DTHIS_IS_CSS_COLOR

include $(ADD_MODULE)


## double-conversion
MODULE_BASE = $(MAKE_BASE)/double-conversion

MODULE_SOURCES = \
  src/bignum-dtoa.cc \
  src/bignum.cc \
  src/cached-powers.cc \
  src/diy-fp.cc \
  src/double-conversion.cc \
  src/fast-dtoa.cc \
  src/fixed-dtoa.cc \
  src/strtod.cc

MODULE_INC_PUBLIC = include
MODULE_INC_PRIVATE = src
#MODULE_DEFS_PUBLIC = DBL_CONV_PUBLIC
#MODULE_DEFS_PRIVATE = DBL_CONV_PRIVATE
#MODULE_CFLAGS = -DTHIS_IS_DBL_CONV

include $(ADD_MODULE)


## duktape
MODULE_BASE = $(MAKE_BASE)/duktape

MODULE_SOURCES = duktape.c
MODULE_INC_PUBLIC = .
MODULE_CCFLAGS = -fstrict-aliasing -fomit-frame-pointer -std=c99 -Wall

include $(ADD_MODULE)


## geojson-vt-cpp
MODULE_BASE = $(MAKE_BASE)/geojson-vt-cpp

MODULE_INC_PUBLIC = include

include $(ADD_MODULE)


## glm
MODULE_BASE = $(MAKE_BASE)/glm

MODULE_INC_PUBLIC = .
MODULE_DEFS_PUBLIC = GLM_FORCE_CTOR_INIT

include $(ADD_MODULE)


## miniz
MODULE_BASE = $(MAKE_BASE)/miniz

MODULE_SOURCES = miniz.c
MODULE_INC_PUBLIC = .

include $(ADD_MODULE)


## sqlite
MODULE_BASE = $(MAKE_BASE)/sqlite3

MODULE_SOURCES = sqlite3.c
MODULE_INC_PUBLIC = .

include $(ADD_MODULE)


## gaml
MODULE_BASE = $(MAKE_BASE)/gaml

#MODULE_FULL_SOURCES = $(wildcard $(MODULE_BASE)/src/*.cpp)
MODULE_SOURCES = src/yaml.cpp
#MODULE_INC_PUBLIC = include
MODULE_INC_PRIVATE = src
MODULE_DEFS_PRIVATE = GAML_LIB_ONLY

include $(ADD_MODULE)


## LERC
MODULE_BASE = $(MAKE_BASE)/lerc/src/LercLib

MODULE_FULL_SOURCES = $(wildcard $(MODULE_BASE)/*.cpp)
MODULE_FULL_SOURCES += $(wildcard $(MODULE_BASE)/Lerc1Decode/*.cpp)

MODULE_INC_PRIVATE = Lerc1Decode
MODULE_INC_PUBLIC = .

include $(ADD_MODULE)

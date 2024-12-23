# Tangram tests

MAKE_BASE := $(GET_MAKE_BASE)

MODULE_BASE = $(MAKE_BASE)

# unit tests
MODULE_SOURCES = \
  unit/curlTests.cpp \
  unit/drawRuleTests.cpp \
  unit/dukTests.cpp \
  unit/fileTests.cpp \
  unit/flyToTest.cpp \
  unit/jobQueueTests.cpp \
  unit/labelsTests.cpp \
  unit/labelTests.cpp \
  unit/layerTests.cpp \
  unit/lngLatTests.cpp \
  unit/mapProjectionTests.cpp \
  unit/meshTests.cpp \
  unit/networkDataSourceTests.cpp \
  unit/sceneImportTests.cpp \
  unit/sceneLoaderTests.cpp \
  unit/sceneUpdateTests.cpp \
  unit/stopsTests.cpp \
  unit/styleMixerTests.cpp \
  unit/styleParamTests.cpp \
  unit/styleSortingTests.cpp \
  unit/styleUniformsTests.cpp \
  unit/textureTests.cpp \
  unit/tileIDTests.cpp \
  unit/tileManagerTests.cpp \
  unit/urlTests.cpp \
  unit/yamlFilterTests.cpp \
  unit/yamlUtilTests.cpp

# mock platform
MODULE_SOURCES += \
  src/mockPlatform.cpp \
  src/gl_mock.cpp \

MODULE_INC_PRIVATE = catch src

include $(ADD_MODULE)

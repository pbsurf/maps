# GLFW

MAKE_BASE := $(GET_MAKE_BASE)

MODULE_BASE = $(MAKE_BASE)/glfw

MODULE_SOURCES = \
  src/context.c \
  src/init.c \
  src/input.c \
  src/monitor.c \
  src/vulkan.c \
  src/window.c

ifneq ($(windir),)
# Windows

MODULE_SOURCES += \
  src/win32_init.c \
  src/win32_joystick.c \
  src/win32_monitor.c \
  src/win32_time.c \
  src/win32_thread.c \
  src/win32_window.c \
  src/wgl_context.c \
  src/egl_context.c \
  src/osmesa_context.c

else
# X11

MODULE_SOURCES += \
  src/x11_init.c \
  src/linux_joystick.c \
  src/x11_monitor.c \
  src/x11_window.c \
  src/xkb_unicode.c \
  src/posix_time.c \
  src/posix_thread.c \
  src/glx_context.c \
  src/egl_context.c \
  src/osmesa_context.c

endif

MODULE_INC_PRIVATE = include
MODULE_DEFS_PRIVATE = _GLFW_X11

include $(ADD_MODULE)

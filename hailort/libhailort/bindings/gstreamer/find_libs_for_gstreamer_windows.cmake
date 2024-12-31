cmake_minimum_required(VERSION 3.5.0)

# CMake variable GSTREAMER_ROOT_DIR defines the location of the gstreamer files.
# It's default value is C:/gstreamer/1.0/msvc_x86_64.
if (NOT GSTREAMER_ROOT_DIR)
    message("Gstreamer Windows compilation - GSTREAMER_ROOT_DIR is not set. default value is C:/gstreamer/1.0/msvc_x86_64")
    set(GSTREAMER_ROOT_DIR "C:/gstreamer/1.0/msvc_x86_64")
endif()
set(GLIB_ROOT_DIR ${GSTREAMER_ROOT_DIR})

# Find the GStreamer library and include directories
find_path(GSTREAMER_INCLUDE_DIRS
    NAMES gst/gst.h
    PATHS ${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0
    REQUIRED
)

find_library(GSTREAMER_LIBRARIES
    NAMES gstreamer-1.0
    PATHS ${GSTREAMER_ROOT_DIR}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GStreamer DEFAULT_MSG
    GSTREAMER_LIBRARIES GSTREAMER_INCLUDE_DIRS
    REQUIRED
)

# Find the GStreamer base library and include directories
find_path(GSTREAMER_BASE_INCLUDE_DIRS
    NAMES gst/base/base.h
    PATHS ${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0
)

find_library(GSTREAMER_BASE_LIBRARIES
    NAMES gstbase-1.0
    PATHS ${GSTREAMER_ROOT_DIR}/lib
)

find_package_handle_standard_args(GStreamerPluginsBase DEFAULT_MSG
    GSTREAMER_BASE_LIBRARIES GSTREAMER_BASE_INCLUDE_DIRS
)

# Find the GStreamer video library and include directories
find_path(GSTREAMER_VIDEO_INCLUDE_DIRS
    NAMES gst/video/video.h
    PATHS ${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0
)

find_library(GSTREAMER_VIDEO_LIBRARIES
    NAMES gstvideo-1.0
    PATHS ${GSTREAMER_ROOT_DIR}/lib
)

find_package_handle_standard_args(GStreamerVideo DEFAULT_MSG
    GSTREAMER_VIDEO_LIBRARIES GSTREAMER_VIDEO_INCLUDE_DIRS
)

# Find the GLib library and include directories
find_path(GLIB_INCLUDE_DIRS
    NAMES glib.h
    PATHS ${GLIB_ROOT_DIR}/include/glib-2.0
        ${GLIB_ROOT_DIR}/lib/glib-2.0/include
    REQUIRED
)

find_library(GLIB_LIBRARIES
    NAMES glib-2.0
    PATHS ${GLIB_ROOT_DIR}/lib
)

find_library(GOBJECT_LIBRARIES
    NAMES gobject-2.0
    PATHS ${GLIB_ROOT_DIR}/lib
)

# Add the directory containing glibconfig.h to the include directories
find_path(GLIBCONFIG_INCLUDE_DIR
    NAMES glibconfig.h
    PATHS ${GLIB_ROOT_DIR}/lib/glib-2.0/include
    REQUIRED
)

find_package_handle_standard_args(GLib DEFAULT_MSG
    GLIB_LIBRARIES GLIB_INCLUDE_DIRS GLIBCONFIG_INCLUDE_DIR
    REQUIRED
)
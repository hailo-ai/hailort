cmake_minimum_required(VERSION 3.5.0)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)
pkg_search_module(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_search_module(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
pkg_search_module(GSTREAMER_PLUGINS_BASE REQUIRED gstreamer-plugins-base-1.0)
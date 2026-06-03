# Top-level project file for ESP32-SIP-Voice
# (ESP-IDF requires the project() call to come from the root CMakeLists,
#  with the application sources living in the `main` component.)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_sip_voice)

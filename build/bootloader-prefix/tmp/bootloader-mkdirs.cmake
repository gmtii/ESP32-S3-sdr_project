# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/gmtii/.espressif/v6.0/esp-idf/components/bootloader/subproject"
  "/home/gmtii/git/sdr_project/build/bootloader"
  "/home/gmtii/git/sdr_project/build/bootloader-prefix"
  "/home/gmtii/git/sdr_project/build/bootloader-prefix/tmp"
  "/home/gmtii/git/sdr_project/build/bootloader-prefix/src/bootloader-stamp"
  "/home/gmtii/git/sdr_project/build/bootloader-prefix/src"
  "/home/gmtii/git/sdr_project/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/gmtii/git/sdr_project/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/gmtii/git/sdr_project/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

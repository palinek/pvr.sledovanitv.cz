[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.txt)
[![Build and run tests](https://github.com/palinek/pvr.sledovanitv.cz/actions/workflows/build.yml/badge.svg?branch=Omega)](https://github.com/palinek/pvr.sledovanitv.cz/actions/workflows/build.yml)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/palinek/job/pvr.sledovanitv.cz/job/Omega/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/palinek%2Fpvr.sledovanitv.cz/branches/)

# sledovanitv.cz PVR
unofficial [sledovanitv.cz](https://sledovanitv.cz) PVR client addon for [Kodi](https://kodi.tv)
(this client is not developed nor officially supported by sledovanitv.cz)

## Currently supported service providers
- [sledovanitv.cz](https://sledovanitv.cz)
- [modernitv.cz](https://modernitv.cz)

## Build instructions

### Linux

1. `git clone --branch master --depth=1 https://github.com/xbmc/xbmc.git`
2. `git clone --depth=1 https://github.com/palinek/pvr.sledovanitv.cz.git`
3. `cd pvr.sledovanitv.cz && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.sledovanitv.cz -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=xbmc/addons -DPACKAGE_ZIP=1 -DADDONS_DEFINITION_DIR="$(pwd)/../xbmc/cmake/addons/addons" ../../xbmc/cmake/addons`
5. `make`
6. `make package-addons`

##### Useful links

* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

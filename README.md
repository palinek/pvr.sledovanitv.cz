# sledovanitv.cz PVR
sledovanitv.cz PVR client addon for [Kodi](http://kodi.tv)

## Build instructions

### Linux

1. `git clone https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/palinek/pvr.sledovanitv.cz.git`
3. `cd pvr.sledovanitv.cz && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.sledovanitv.cz -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=xbmc/addons -DPACKAGE_ZIP=1 -DADDONS_DEFINITION_DIR="$(pwd)/../xbmc/cmake/addons/addons" ../../xbmc/cmake/addons`
5. `make`
6. `make package-addons`

##### Useful links

* [Kodi's PVR user support] (http://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support] (http://forum.kodi.tv/forumdisplay.php?fid=136)

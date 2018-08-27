# sledovanitv.cz PVR
sledovanitv.cz PVR client addon for [Kodi] (http://kodi.tv)

## Build instructions

### Linux

1. `git clone -b Krypton --single-branch https://github.com/xbmc/xbmc.git`
2. `git clone -b Krypton --single-branch https://github.com/palinek/pvr.sledovanitv.git`
3. `cd pvr.sledovanitv && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.sledovanitv -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=xbmc/addons -DPACKAGE_ZIP=1 -DADDONS_DEFINITION_DIR="$(pwd)/../xbmc/project/cmake/addons/addons" ../../xbmc/project/cmake/addons`
5. `make`
6. `make package-addons`

##### Useful links

* [Kodi's PVR user support] (http://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support] (http://forum.kodi.tv/forumdisplay.php?fid=136)

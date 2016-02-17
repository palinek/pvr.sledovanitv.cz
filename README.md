# sledovanitv.cz PVR
sledovanitv.cz PVR client addon for [Kodi] (http://kodi.tv)

## Build instructions

### Linux

1. `git clone https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/PepaRokos/pvr.sledovanitv.git`
3. `cd pvr.sledovanitv && cp xbmc/ ../ -R && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.sledovanitv -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/project/cmake/addons`
5. `make`

##### Useful links

* [Kodi's PVR user support] (http://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support] (http://forum.kodi.tv/forumdisplay.php?fid=136)

# HDSDR ExtIO DLL for SDR MK1.5 'Andrus' #

This is a repository for ExtIO DLL source code created for SDR MK1.5 'Andrus' radios. However, an empty DLL framework is also provided at Downloads area as an example ExtIO DLL implementation to help creating DLL-s on Microsoft Visual Studio 2008 C++ environment, as there seems to be a lack of such information otherwise.

This DLL is somewhat backward-compatibile with SDR MK1 radios and early firmware releases of MK1.5. However, on such cases only the audio interface with 2x 48kHz bandwidth is supported.

To unlock the full potential of SDR MK1.5 (and MK1) radio with HDSDR, one has to:

  * Upgrade your radio to at least version 1.70 of the [SDR MK1.5 Firmware](http://code.google.com/p/sdr-mk15/). The files to be downloaded are:
    * [Firmware V1.70 .HEX file](http://code.google.com/p/sdr-mk15/downloads/detail?name=SDR_MK1.5_V1.70.hex&can=2&q=)
    * [Application Note AN-001: Firmware Update Procedure](http://code.google.com/p/sdr-mk15/downloads/detail?name=SDR_MK15_AN-001_10.pdf&can=2&q=)
    * [Atmel Flip Utility](http://www.atmel.com/tools/FLIP.aspx) to actually perform firmware update (See AN-001 about what and how)
  * Install the libusb-win32 driver found at the [Downloads section](http://code.google.com/p/extiodll/downloads/list). The file to be downloaded is:
    * [libusb-win32 v1.2.6.0 Driver Binaries with SDR MK1.5 specific .INF files](http://code.google.com/p/extiodll/downloads/detail?name=SDR_MK1.5_Drivers_Libusb-w32-1260_CDC-inf.zip&can=2&q=)
  * Copy the latest ExtIO DLL file to HDSDR program directory. The latest stable version can be found at the download area, the latest development version can be downloaded directly from source trunk:
    * [ExtIO DLL Ver 1.3](http://code.google.com/p/extiodll/downloads/detail?name=ExtIO_SDR_MK15.zip&can=2&q=) or [Latest Development Version (Ver 1.39 currently)](http://extiodll.googlecode.com/svn/trunk/ExtIO_SDR_MK15.dll)
    * [Quick Installation Instructions](http://code.google.com/p/extiodll/downloads/detail?name=SDR_MK15_Quick_Installation_11.pdf&can=2&q=)


The libusb-win32 version currently in use by this project is 1.2.6.0. For newer versions, please check the [libusb-win32 project home](http://libusb-win32.sourceforge.net). (You will still need the SDR MK1.5-specific Bulk interface .INF file than can be separately downloaded at the [Downloads section](http://code.google.com/p/extiodll/downloads/list) of this repository).



---


![http://sdr-mk1.googlecode.com/svn/wiki/88x31.png](http://sdr-mk1.googlecode.com/svn/wiki/88x31.png)

The work is licensed under the [Creative Commons](http://creativecommons.org/licenses/) [Attribution-NonCommercial-ShareAlike (CC BY-NC-SA)](http://creativecommons.org/licenses/by-nc-sa/3.0/) License. Basically, you are free to remix, tweak, and build upon this work non-commercially, as long as you credit the original work properly and license your new creations under the identical terms. You have to, however, ask for permission if you are planning to use the material or inherited works commercially.
# llink media server project

by Jörgen Lundman, 2005-2023.
```<lundman@lundman.net>```

llink icon by Ron Wood

## Read more about llink at:
https://www.lundman.net/wiki/index.php/Llink

## Ports:

The default for llink.conf is to use port 8001, and jukebox.conf port
8002. You need to open your firewall for which ever port you wish to
use.

Also, if you want UPnP auto discovery to work, it needs port 1900 UDP
to be open.

## Building llink for macOS from source

llink for macOS has two main parts:

* The llink daemon and support programs: llink/
* The macOS UI .app: llink-macOS/

To build llink you must first build the daemon and support program portion
of the application. Before anything will build properly the following 
additional open source projects are required (beyond the usual bits like 
autoconf, automake, libtool, etc.):

* expat
* openssl
* libdvdcss
* libintl (gettext)
* gdbm
* libiconv 
* zlib

These should installed via something like MacPorts or compiled by hand.
gcc is not required to build llink; everything will build with clang.

Next, build the support libraries for the daemon portion of the
application. Should the configure script fail with autoconf problems, run

```
autoreconf --force --install
```
### clinkc-2.4
```
./configure --disable-libxml2
make install
```
### clinkcav-trunk
```
./configure --disable-libxml2
make install
```
### libdvdnav-4.2.0
```
./configure
make install
```
### libdvdread-4.2.0.plus
```
./configure
make install
```
### llink-2.3.2
```
./configure --enable-dvdcss --enable-clinkc LIBS=-lexpat CPPFLAGS=-Ilion/src
make
```
When everything has finished building, four files must be copied into the 
llink-macOS/llink-macOS directory. These are used when bundling the app:
```
llink-2.3.2/undvd/undvd
llink-2.3.2/unrar-3.7.8-seek/unrar
llink-2.3.2/src/llink 
{/opt|/usr}/local/lib/libclinkcav.0.dylib
```
### llink-macOS (the GUI application)
Once the four aforementioned files have been copied into place, open
llink.xcodeproj in Xcode. You will likely need to do the code signing
dance from the top level llink project -> Signing & Capabilities.
When that is completed, run Product -> Build from the Xcode menu.
The resulting llink.app will contain the macOS UI app as well as the daemon.


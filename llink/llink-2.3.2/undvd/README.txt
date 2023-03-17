
undvd / uniso / unimg / unnav

The basic idea is that; since the code to spawn "unrar" to list and play from RAR archives is rather robust and heavily debugged. It would be quite desirable to simply replace "unrar" with a different binary "undvd" to list and play DVD images.

The goal is then to have identical program arguments and output as that of "unrar".

To list:

	"unrar v -v -c- -p- -y -cfg- -- \"filename.rar\""

Output should look like:

UNRAR 3.71 beta 1 freeware      Copyright (c) 1993-2007 Alexander Roshal

Archive a test.rar

Pathname/Comment
                  Size   Packed Ratio  Date   Time     Attr      CRC   Meth Ver
-------------------------------------------------------------------------------
 directory1/directory2/file2.avi
                     7       17 242% 04-12-07 14:11 -rw-r--r-- 16B28489 m3b 2.9
 directory1/file1.avi
                     6       16 266% 04-12-07 14:10 -rw-r--r-- 363A3020 m3b 2.9
 file with spaces.avi
                     0        8   0% 04-12-07 14:10 -rw-r--r-- 00000000 m3b 2.9
 file.avi
                     6       16 266% 04-12-07 14:11 -rw-r--r-- 363A3020 m3b 2.9
 directory1/directory2
                     0        0   0% 04-12-07 14:11 drwxr-xr-x 00000000 m0  2.0
 directory with spaces
                     0        0   0% 04-12-07 14:10 drwxr-xr-x 00000000 m0  2.0
 directory1
                     0        0   0% 04-12-07 14:11 drwxr-xr-x 00000000 m0  2.0
-------------------------------------------------------------------------------
    7               19       57 300%



To play:

					 "unrar p -inul -c- -p- -y -cfg- -sk12345678 -- \"filename.rar\" \"name_inside_rar\""
                     (name_inside_rar can not start with leading "/".)

And simply send bytes on stdout. Until EOF, or terminated.



Audio Select:

PCH/NMT when playing VOB/MPG will simply take the very first audio packet it receives and use that for its audio. You can not change tracks. So this code can simply change the audio track of the first packet, to that of the stream we want. (Possibly get a blip). It can discard audio stream packets we don't want. It can replace audio stream packets all the way through (81 <-> 83), etc. All work, all with pros&cons.



TODO:

List could use the date of the file, instead of statics.

Handle sub-title streams as well, would be nice.

It would be even more awesome if it stripped out the dvd packets not wanted (other languages etc) instead of the way we "select" a channel now.


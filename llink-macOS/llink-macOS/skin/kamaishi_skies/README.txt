Kamaishi_skies is a skin for llink with support for imbd information
and two separate visual styles depending on background.
==Configuration===
In llink.conf change SKIN line to:
SKIN|path=./skin/kamaishi_skies/|PAGE_SIZE=10|SIZE_TYPE=human|PASSES=2
And
RAND|FREQ=30|MAX=2
MAX should be the same as the amount of rand*.jpg files you have. 

Light colored pictures should have low numbers and to get black text 
and white background change the value in head.html

function changecolor(){
		if (value<=1){
		
and in
function show2(x,plot,rating,tajm,genres){
		if (value<=1){
to value<=number.of.light.background.rand#.jpg
==Redirection==
Remember to include *.jpg in the redirected path to get covers
TYPE|name=extra|cmd=redirect|ext=*.xml/*.jpg|filename=.|args=/redirected.folder/
==Iso and nfs==
This skin has support for playing iso's from within llink if an nfs 
share is set up on the PCH. in line_iso.html change
<a href="file:///opt/sybhttpd/localhost.drives/NETWORK_SHARE/nas::media:/<!--LLINK_FILE_URL-->"
To your path.
In llink.conf add
TYPE|name=Iso|ext=*.iso/*.img|filename=line_iso.html 
remember to remove *.iso/*.img from the line_movie.html line.
==Navigation==
Next and previous pages can be reached by "right" and "left" as well as
standard color navigation. From llink 2.2.0 pressing "stop" will change the sorting order and
"home" will take you to the root

==Notes==
Use PiXL ( http://wieslander.eu/wiki/index.php?title=Llink:pixl ) or imdbit for imdb scraping
==Cred==
This couldnt have been done without support from networkmediatank.com 
forum and users:
Lundman (llink http://www.lundman.net/wiki/index.php/Llink), 
wesleyelder (imbdit http://code.google.com/p/imdbit/), 
E.O. (syabas) and dc11ab
Background images comes from http://www.aeonismine.com/ where you can
find many more.

          .-'-.          
        /`     |__      
      /`  _.--`-,-`     
      '-|`   a '<-.   []
        \     _\__) \=` 
         C_  `    ,_/   
           | ;----'     

PopEye the sailor man!
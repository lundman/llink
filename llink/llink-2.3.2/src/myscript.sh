#!/bin/sh
#
# Example script for llink user scripts.
#
# llink.conf should have something like:
#    SCRIPT|name=myscript|path=./myscript.sh|PIN=NO
#
# The html templates for the skin, or menu.html, should have:
#    <a href="<!--LLINK_FILE_URL-->&menu&myscript=<!--LLINK_FILE_URL-->"
#
# The script is passed the media selected at menu time as $1.
#
# If the script's output contains lines starting with "STATUS:" it is
# used as the STATUS_MESSAGE in llink MACROS.
#
line=`ls -l $1`

echo "STATUS:User script ran for $line"

exit 0

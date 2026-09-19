#!/bin/bash
cd /home/kjell/git/harvac/other/dos/ncd
# Use dosbox with a script
dosbox -c "mount c /home/kjell/git/harvac/other/dos/ncd" -c "c:" -c "ncd.com" -c "exit" 2>&1

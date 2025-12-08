#!/bin/bash

rm -rf toGround;
mkdir -p toGround;

demos_folder="demos"
demos_shareware="e1m7-607 impfight m1-fast m1-normal m1-simple"

statdump_filepath=toGround/run-wsl.txt

runid=1
for demo in $demos_shareware
do
    ./src/bin/opssat-doom -nosound -nomusic -nosfx -runid $runid -longtics -iwad demos/doom.wad -cdemo $demos_folder/$demo -statdump ${statdump_filepath} >> toGround/doom.log 2>&1;
    if [ -f "${statdump_filepath}" ]; then
        if diff ${statdump_filepath} ${demos_folder}/${demo}.txt; then
            result="OK"
        else
            result="ERROR"
            diff ${statdump_filepath} ${demos_folder}/${demo}.txt >> toGround/doom.log 2>&1
        fi
        echo ${result} - ${demo} >> toGround/results.log 2>&1;
    fi

    runid=$((runid+1))
done
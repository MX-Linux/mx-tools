#!/bin/bash

##translate desktop file strings to appropriate po file

RESOURCE="$(basename $(dirname $(pwd)))-desktop"
echo $RESOURCE

TARGET_FOLDER="desktop-in/"

for file in ../data/*.desktop; do
    cp -- "$file" "${TARGET_FOLDER}$(basename "$file").in" || exit 1
	
done

sed -i '/^Name\[/d' ${TARGET_FOLDER}*.desktop.in
sed -i '/^Comment\[/d' ${TARGET_FOLDER}*.desktop.in

##add translator comments

sed -i '/^Name=.*/i \# The desktop entry name will be displayed within the menu.' ${TARGET_FOLDER}*.desktop.in
sed -i '/^Comment=.*/i \# # The desktop entry comment will be shown within the menu.' ${TARGET_FOLDER}*.desktop.in

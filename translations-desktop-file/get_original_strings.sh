#!/bin/bash

##translate desktop file strings to appropriate po file

RESOURCE="$(basename $(dirname $(pwd)))-desktop"
echo $RESOURCE
[ -f uploadlist.txt ] && rm uploadlist.txt
[ -f errorcheck.txt ] && rm errorcheck.txt

lang="am ar bg ca cs da de el es et eu fa fi fr fr_BE he_IL hi hr hu id is it ja kk ko lt mk mr nb nl pl pt pt_BR ro ru sk sl sq sr sv tr uk zh_CN zh_TW"


LIST="../*.desktop"
TARGET_PO_FOLDER="po"

for file in $LIST; do
ORIGINALCOMMENT="$(grep "Comment=" $file | cut -d"=" -f2)"
echo "Original Comment is " $ORIGINALCOMMENT
ORIGINALNAME="$(grep "Name=" $file | cut -d"=" -f2)"
echo "Original Name is " $ORIGINALNAME
	for val in $lang; do
		flag=false
		NAME="$(grep Name\\[$val\\] $file | cut -d"=" -f2)"
        NAME=$(sed -e 's/[&\\/]/\\&/g; s/$/\\/' -e '$s/\\$//' <<<"$NAME")
		COMMENT="$(grep Comment\\[$val\\] $file | cut -d"=" -f2)"
        COMMENT=$(sed -e 's/[&\\/]/\\&/g; s/$/\\/' -e '$s/\\$//' <<<"$COMMENT")
		if [ -n "$NAME" ]; then
			if [ "$NAME" != "$ORIGINALNAME" ]; then
				sed -i "/$ORIGINALNAME/{n;s/.*/msgstr \"$NAME\"/}" $TARGET_PO_FOLDER/${RESOURCE}_${val}.po
				if [ $? != 0 ]; then
					echo $val >> errorcheck.txt
				fi
				flag=true
			fi
	    fi
	    if [ -n "$COMMENT" ]; then
	    	if [ "$COMMENT" != "$ORIGINALCOMMENT" ]; then
				sed -i "/$ORIGINALCOMMENT/{n;s/.*/msgstr \"$COMMENT\"/}" $TARGET_PO_FOLDER/${RESOURCE}_${val}.po
				if [ $? != 0 ]; then
					echo $val >> errorcheck.txt
				fi
				flag=true
			fi 
	    fi
	    if [ "$flag" = "true" ]; then
	    	echo ${RESOURCE}_${val}.po >> uploadlist.txt
	    fi
	done
done

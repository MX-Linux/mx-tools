#!/bin/bash

#make po files

RESOURCE="$(basename $(dirname $(pwd)))-desktop"

lang="am ar bg ca cs da de el es et eu fa fi fr fr_BE he_IL hi hr hu id is it ja kk ko lt mk mr nb nl pl pt pt_BR ro ru sk sl sq sr sv tr uk zh_CN zh_TW"

make_po()
{
mkdir -p po
for val in $lang; do
    if [ ! -e po/"${RESOURCE}_${val}.po" ]; then
        msginit --input=pot/"$RESOURCE".pot --no-translator --locale=$val --output=po/"${RESOURCE}_${val}.po"
    else
        msgmerge --update po/"${RESOURCE}_${val}.po" pot/$RESOURCE.pot
    fi
    sed -i 's/ASCII/UTF-8/' po/"${RESOURCE}_${val}.po"
done
}

make_pot()
{
if [ ! -d "pot" ]; then
    mkdir pot
fi
xgettext --language Desktop --add-comments -o pot/$RESOURCE.pot desktop-in/*.desktop.in
}


po()
{
    make_pot
    make_po
}

pot(){
	make_pot
}

main()
{
    $1 
    $2
}

main "$@"



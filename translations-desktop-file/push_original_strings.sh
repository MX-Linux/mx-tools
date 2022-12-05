#!/bin/bash

##push local po files to transifex
##this is destructive on transifex, so problaby only useful when initializing a new resource
##or possibly migrating one resource to another
#there is a confirmation step
#and the resource must exist

RESOURCE="$(basename $(dirname $(pwd)))-desktop"

tx push -r antix-development.${RESOURCE} --translations --force


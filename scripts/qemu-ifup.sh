#!/bin/sh

brctl addbr $OSV_BRIDGE
brctl stp $OSV_BRIDGE off
brctl addif $OSV_BRIDGE $1
ifconfig $1 up

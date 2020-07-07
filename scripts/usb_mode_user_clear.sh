#!/bin/sh

if [ -z "$1" ]; then
    exit 1;
fi

/usr/sbin/usb_moded_util -U $1

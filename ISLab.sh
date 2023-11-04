#!/bin/bash

if [[ -z $1 ]]; then
	echo "需要輸入介面名稱!"
	echo "舉例: ./ECU.sh vcan0"
	exit 1
fi

interface=$1

./Battery $interface & ./Communication & ./Dashboard $interface & ./CVE-2022-33218 & ./ICSim vcan0

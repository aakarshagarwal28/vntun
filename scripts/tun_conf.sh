#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <interface> <ip> <prefix>"
    exit 1
fi

IFACE="$1"
IP="$2"
PREFIX="$3"

sudo ip addr add "${IP}/${PREFIX}" dev "$IFACE"
sudo ip link set "$IFACE" up
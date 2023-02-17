#!/bin/sh
#
# Set up script to run the Defensics CAN-bus fuzzer against a local
# loop-backed undut instance using only socketcan (i.e. no physical CAN
# hardware involved, just one system running both the fuzzer and DuT.
#
# This script is to be run by the superuser.  It loads the various CAN
# kernel modules, then sets up a virtual loopback socketcan device
# (vcan0) for use by applications such as "undut".

# Install the can modules - change this to suit your distro and kernel

modprobe can
modprobe can_raw
modprobe vcan

# Show the installed CAN kernel modules. The output should look like
# this example, from a CentOS 7 sytem:
#
# vcan                   12726  0 
# can_raw                17120  0 
# can                    36567  1 can_raw

lsmod | grep can

# Set up the vcan0 device

ip link add dev vcan0 type vcan
ip link set up vcan0

# Show the results. Should look something like:
#
# 4: vcan0: <NOARP,UP,LOWER_UP> mtu 16 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
#     link/can 

ip link show vcan0

#!/bin/bash

# Quick script to SSH into a specific VM
# Usage: ./ssh_to_vm.sh 01

NETID="your_netid"  # Replace with your NetID
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"

if [ -z "$1" ]; then
    echo "Usage: $0 <machine_number>"
    echo "Example: $0 01"
    exit 1
fi

MACHINE=$(printf "%02d" $1)
ssh "${NETID}@${BASE_HOST}${MACHINE}.${DOMAIN}" -t "cd ${REMOTE_DIR}; exec bash"

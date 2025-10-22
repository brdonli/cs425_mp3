#!/bin/bash

# Script to kill all running main processes on all VMs

NETID="rayans2"  # Replace with your NetID
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${RED}Killing all processes on all machines...${NC}"

for machine in "${MACHINES[@]}"; do
    echo -e "Killing processes on ${BASE_HOST}${machine}..."
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "pkill -9 -f './build/main'" &
done

wait
echo -e "${GREEN}✓ All processes killed${NC}"

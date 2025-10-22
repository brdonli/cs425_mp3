#!/bin/bash

# Simple deployment script that just pulls code and builds
# Useful for quick updates without launching all terminals

NETID="your_netid"  # Replace with your NetID
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"
GIT_BRANCH="main"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Deploying code to all machines...${NC}"

for machine in "${MACHINES[@]}"; do
    echo -e "${YELLOW}Deploying to ${BASE_HOST}${machine}...${NC}"
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "cd ${REMOTE_DIR} && git pull origin ${GIT_BRANCH} && make -j4" &
done

wait
echo -e "${GREEN}✓ Deployment complete!${NC}"

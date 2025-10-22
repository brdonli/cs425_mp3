#!/bin/bash

# Initialize build directories on all VMs
# Run this once if build directories don't exist

NETID="rayans2"
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Initializing build directories on all VMs...${NC}"
echo ""

for machine in "${MACHINES[@]}"; do
    echo -e "${YELLOW}Building ${BASE_HOST}${machine}...${NC}"
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" \
        "cd ${REMOTE_DIR} && make -j4" &
done

wait
echo ""
echo -e "${GREEN}✓ Build directories initialized on all machines${NC}"
echo ""
echo "Now you can run:"
echo "  ./tmux_launch.sh     - Launch all VMs in tmux"
echo "  ./deploy_and_launch.sh - Launch in separate terminals"
echo ""

#!/bin/bash

# Script that prints all SSH commands for manual execution
# Use this if automatic terminal launching doesn't work

NETID="rayans2"
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"

BASE_PORT=12340
INTRODUCER_HOST="${BASE_HOST}01.${DOMAIN}"
INTRODUCER_PORT=$BASE_PORT

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Manual SSH Commands${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}First, update code on all VMs (run this in your current terminal):${NC}"
echo ""
echo -e "${CYAN}for i in {01..10}; do ssh ${NETID}@${BASE_HOST}\$i.${DOMAIN} \"cd ${REMOTE_DIR} && git pull && make -C build -j4\" & done; wait${NC}"
echo ""
echo ""
echo -e "${YELLOW}Then, open 10 terminals and run these commands:${NC}"
echo ""

for i in "${!MACHINES[@]}"; do
    machine="${MACHINES[$i]}"
    port=$((BASE_PORT + i))
    hostname="${BASE_HOST}${machine}.${DOMAIN}"

    if [ "$machine" == "01" ]; then
        remote_cmd="cd ${REMOTE_DIR} && ./build/main ${hostname} ${port}"
        title="Introducer - VM${machine}"
    else
        remote_cmd="cd ${REMOTE_DIR} && ./build/main ${hostname} ${port} ${INTRODUCER_HOST} ${INTRODUCER_PORT}"
        title="VM${machine}"
    fi

    echo -e "${GREEN}# Terminal $((i+1)): ${title}${NC}"
    echo -e "${CYAN}ssh ${NETID}@${hostname}${NC}"
    echo -e "${CYAN}${remote_cmd}${NC}"

    if [ "$machine" != "01" ]; then
        echo -e "${YELLOW}# Then type: join${NC}"
    fi
    echo ""
done

echo ""
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Tips:${NC}"
echo "  - Use a terminal multiplexer like tmux for easier management"
echo "  - VM01 is the introducer, it doesn't need to join"
echo "  - VM02-10 need to type 'join' after starting"
echo ""

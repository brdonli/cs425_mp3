#!/bin/bash

# Launch all VMs in a single tmux session with multiple panes
# Much easier to manage than separate terminal windows

NETID="rayans2"
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"
GIT_BRANCH="main"

BASE_PORT=12340
INTRODUCER_HOST="${BASE_HOST}01.${DOMAIN}"
INTRODUCER_PORT=$BASE_PORT
SESSION_NAME="mp3_cluster"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Check if tmux is installed
if ! command -v tmux &> /dev/null; then
    echo -e "${RED}Error: tmux is not installed${NC}"
    echo "Install it with: brew install tmux (macOS) or apt install tmux (Linux)"
    exit 1
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Tmux-based VM Launcher${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Kill existing session if it exists
tmux kill-session -t ${SESSION_NAME} 2>/dev/null

# Update code on all VMs first
echo -e "${YELLOW}Updating code on all VMs...${NC}"
for machine in "${MACHINES[@]}"; do
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "cd ${REMOTE_DIR} && git pull origin ${GIT_BRANCH} && make -C build -j4" &
done
wait
echo -e "${GREEN}✓ All VMs updated${NC}"
echo ""

# Create new tmux session
echo -e "${YELLOW}Creating tmux session...${NC}"
tmux new-session -d -s ${SESSION_NAME}

# Create panes for all VMs
for i in "${!MACHINES[@]}"; do
    machine="${MACHINES[$i]}"

    if [ $i -gt 0 ]; then
        # Split window for all machines except the first
        if [ $i -eq 5 ]; then
            # After 5 panes, create a new window
            tmux new-window -t ${SESSION_NAME}
        else
            tmux split-window -t ${SESSION_NAME}
            tmux select-layout -t ${SESSION_NAME} tiled
        fi
    fi
done

# Send commands to each pane
for i in "${!MACHINES[@]}"; do
    machine="${MACHINES[$i]}"
    port=$((BASE_PORT + i))
    hostname="${BASE_HOST}${machine}.${DOMAIN}"

    # Determine which window the pane is in
    if [ $i -lt 5 ]; then
        window=0
        pane=$i
    else
        window=1
        pane=$((i - 5))
    fi

    if [ "$machine" == "01" ]; then
        # Introducer
        cmd="ssh ${NETID}@${hostname} -t 'cd ${REMOTE_DIR} && ./build/main ${hostname} ${port}'"
    else
        # Regular node
        cmd="ssh ${NETID}@${hostname} -t 'cd ${REMOTE_DIR} && ./build/main ${hostname} ${port} ${INTRODUCER_HOST} ${INTRODUCER_PORT}'"
    fi

    # Send the SSH command to the pane
    tmux send-keys -t ${SESSION_NAME}:${window}.${pane} "$cmd" C-m

    # For non-introducer nodes, prepare the join command (but don't send it yet)
    if [ "$machine" != "01" ]; then
        sleep 0.5
        # The user will need to type 'join' manually in each pane
    fi
done

# Adjust layout
tmux select-layout -t ${SESSION_NAME}:0 tiled
if [ ${#MACHINES[@]} -gt 5 ]; then
    tmux select-layout -t ${SESSION_NAME}:1 tiled
fi

# Select first pane
tmux select-pane -t ${SESSION_NAME}:0.0

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Tmux session created!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}To attach to the session:${NC}"
echo -e "  tmux attach -t ${SESSION_NAME}"
echo ""
echo -e "${YELLOW}Inside tmux:${NC}"
echo -e "  Ctrl+B then arrow keys - Navigate between panes"
echo -e "  Ctrl+B then [ - Scroll mode (q to exit)"
echo -e "  Ctrl+B then z - Zoom current pane (toggle)"
echo -e "  Ctrl+B then d - Detach (session keeps running)"
echo ""
echo -e "${YELLOW}To type in all panes at once (useful for 'join'):${NC}"
echo -e "  Ctrl+B then :setw synchronize-panes on"
echo -e "  Then type 'join' - it will go to all panes"
echo -e "  Ctrl+B then :setw synchronize-panes off"
echo ""
echo -e "${YELLOW}To kill the session:${NC}"
echo -e "  tmux kill-session -t ${SESSION_NAME}"
echo ""

# Automatically attach
tmux attach -t ${SESSION_NAME}

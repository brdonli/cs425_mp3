#!/bin/bash

# Configuration
NETID="rayans2"  # Replace with your NetID
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"
GIT_BRANCH="main"  # Change if using a different branch

# Port configuration - each machine gets a unique port
BASE_PORT=12340
INTRODUCER_HOST="${BASE_HOST}01.${DOMAIN}"
INTRODUCER_PORT=$BASE_PORT

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}MP3 Deployment and Launch Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Function to run command on a remote machine
run_remote() {
    local machine=$1
    local command=$2
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "$command"
}

# Step 1: Pull latest code on all machines
echo -e "${YELLOW}Step 1: Pulling latest code on all machines...${NC}"
for machine in "${MACHINES[@]}"; do
    echo -e "Updating ${BASE_HOST}${machine}..."
    run_remote "$machine" "cd ${REMOTE_DIR} && git pull origin ${GIT_BRANCH}" &
done
wait
echo -e "${GREEN}✓ Code pulled on all machines${NC}"
echo ""

# Step 2: Build on all machines
echo -e "${YELLOW}Step 2: Building on all machines...${NC}"
for machine in "${MACHINES[@]}"; do
    echo -e "Building on ${BASE_HOST}${machine}..."
    run_remote "$machine" "cd ${REMOTE_DIR} && make -j4" &
done
wait
echo -e "${GREEN}✓ Build completed on all machines${NC}"
echo ""

# Step 3: Launch terminals for each machine
echo -e "${YELLOW}Step 3: Launching SSH terminals...${NC}"

# Detect terminal emulator
TERMINAL=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - use iTerm2 if available, otherwise Terminal.app
    if command -v osascript &> /dev/null; then
        if osascript -e 'application "iTerm" info' &>/dev/null; then
            TERMINAL="iterm"
        else
            TERMINAL="macos"
        fi
    fi
elif command -v gnome-terminal &> /dev/null; then
    TERMINAL="gnome"
elif command -v konsole &> /dev/null; then
    TERMINAL="konsole"
elif command -v xterm &> /dev/null; then
    TERMINAL="xterm"
fi

if [ -z "$TERMINAL" ]; then
    echo -e "${YELLOW}Warning: Could not detect terminal emulator${NC}"
    echo -e "${YELLOW}Printing SSH commands instead. Run them manually:${NC}"
    echo ""
    TERMINAL="manual"
fi

# Launch terminals based on OS
for i in "${!MACHINES[@]}"; do
    machine="${MACHINES[$i]}"
    port=$((BASE_PORT + i))
    hostname="${BASE_HOST}${machine}.${DOMAIN}"

    # Build the command to run on the remote machine
    if [ "$machine" == "01" ]; then
        # Machine 01 is the introducer
        remote_cmd="cd ${REMOTE_DIR} && ./build/main ${hostname} ${port}"
        title="Introducer - VM${machine}"
    else
        # Other machines connect to introducer
        remote_cmd="cd ${REMOTE_DIR} && ./build/main ${hostname} ${port} ${INTRODUCER_HOST} ${INTRODUCER_PORT}"
        title="VM${machine}"
    fi

    echo -e "Launching terminal for ${BASE_HOST}${machine} (port ${port})..."

    case $TERMINAL in
        iterm)
            # iTerm2 on macOS
            osascript <<EOF &
tell application "iTerm"
    create window with default profile
    tell current session of current window
        write text "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'"
        set name to "${title}"
    end tell
end tell
EOF
            ;;
        macos)
            # macOS Terminal.app
            osascript <<EOF &
tell application "Terminal"
    do script "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'"
    set custom title of front window to "${title}"
end tell
EOF
            ;;
        gnome)
            gnome-terminal --title="${title}" --tab -- bash -c "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'" &
            ;;
        konsole)
            konsole --new-tab --title "${title}" -e bash -c "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'" &
            ;;
        xterm)
            xterm -title "${title}" -e bash -c "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'" &
            ;;
        manual)
            # Print commands for manual execution
            echo "# Terminal ${i}: ${title}"
            echo "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'"
            echo ""
            ;;
    esac

    sleep 0.5  # Small delay to avoid overwhelming the system
done

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Deployment Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "Introducer: ${INTRODUCER_HOST}:${INTRODUCER_PORT}"
echo -e "VMs launched: ${#MACHINES[@]}"
echo ""
echo -e "${YELLOW}Available commands in each terminal:${NC}"
echo -e "  join               - Join the network (not for introducer)"
echo -e "  leave              - Leave the network"
echo -e "  list_mem           - List membership"
echo -e "  list_self          - Show self info"
echo -e "  display_suspects   - Show suspected nodes"
echo -e "  display_protocol   - Show current protocol"
echo -e "  switch <mode>      - Switch protocol (gossip/ping suspect/nosuspect)"
echo ""

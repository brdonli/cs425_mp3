#!/bin/bash

# Script to launch just 3 VMs for testing
# Useful for quick tests before scaling to 10 VMs

NETID="rayans2"  # Replace with your NetID
MACHINES=(01 02 03)  # Just 3 VMs for testing
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"
GIT_BRANCH="main"

BASE_PORT=12340
INTRODUCER_HOST="${BASE_HOST}01.${DOMAIN}"
INTRODUCER_PORT=$BASE_PORT

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Launching 3 VMs for testing...${NC}"

# Pull and build
echo -e "${YELLOW}Updating code and building...${NC}"
for machine in "${MACHINES[@]}"; do
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "cd ${REMOTE_DIR} && git pull origin ${GIT_BRANCH} && make -j4" &
done
wait
echo -e "${GREEN}✓ Ready${NC}"

# Detect terminal
TERMINAL=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    if command -v osascript &> /dev/null; then
        if osascript -e 'application "iTerm" info' &>/dev/null; then
            TERMINAL="iterm"
        else
            TERMINAL="macos"
        fi
    fi
elif command -v gnome-terminal &> /dev/null; then
    TERMINAL="gnome"
elif command -v xterm &> /dev/null; then
    TERMINAL="xterm"
fi

if [ -z "$TERMINAL" ]; then
    echo -e "${YELLOW}Warning: Could not detect terminal. Printing commands:${NC}"
    TERMINAL="manual"
fi

# Launch 3 terminals
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

    case $TERMINAL in
        iterm)
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
        xterm)
            xterm -title "${title}" -e bash -c "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'" &
            ;;
        manual)
            echo "# ${title}"
            echo "ssh ${NETID}@${hostname} -t '${remote_cmd}; exec bash'"
            echo ""
            ;;
    esac
    sleep 0.5
done

echo -e "${GREEN}✓ 3 VMs launched${NC}"
echo "VM01: Introducer (just use commands)"
echo "VM02, VM03: Type 'join' to join the network"

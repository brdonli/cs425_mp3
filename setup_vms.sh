#!/bin/bash

# One-time setup script to clone repo on all VMs

NETID="rayans2"  # Replace with your NetID
MACHINES=(01 02 03 04 05 06 07 08 09 10)
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="cs425_mp3"
GIT_REPO_URL="https://github.com/brdonli/cs425_mp3.git"  # Replace with your git repo URL

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

if [ -z "$GIT_REPO_URL" ]; then
    echo -e "${RED}Error: Please set GIT_REPO_URL in this script${NC}"
    echo "Edit setup_vms.sh and set your git repository URL"
    exit 1
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}VM Setup Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "This will:"
echo "  1. Clone git repo on all VMs"
echo "  2. Build the project on all VMs"
echo "  3. Configure git settings"
echo ""
echo "Repository: $GIT_REPO_URL"
echo "Target directory: ~/$REMOTE_DIR"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 1
fi

# Function to run command on remote machine
run_remote() {
    local machine=$1
    local command=$2
    ssh "${NETID}@${BASE_HOST}${machine}.${DOMAIN}" "$command"
}

echo ""
echo -e "${YELLOW}Step 1: Testing SSH connections...${NC}"
for machine in "${MACHINES[@]}"; do
    echo -n "Testing ${BASE_HOST}${machine}... "
    if run_remote "$machine" "echo ok" &>/dev/null; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗ Failed${NC}"
        echo "Please ensure you can SSH to ${BASE_HOST}${machine}.${DOMAIN}"
        exit 1
    fi
done

echo ""
echo -e "${YELLOW}Step 2: Cloning repository on all VMs...${NC}"
for machine in "${MACHINES[@]}"; do
    echo "Cloning to ${BASE_HOST}${machine}..."
    run_remote "$machine" "rm -rf ${REMOTE_DIR} && git clone ${GIT_REPO_URL} ${REMOTE_DIR}" &
done
wait
echo -e "${GREEN}✓ Repository cloned on all machines${NC}"

echo ""
echo -e "${YELLOW}Step 3: Setting up build system on all VMs...${NC}"
for machine in "${MACHINES[@]}"; do
    echo "Setting up ${BASE_HOST}${machine}..."
    run_remote "$machine" "cd ${REMOTE_DIR} && module load cmake && cmake -S . -B build && make -C build -j4" &
done
wait
echo -e "${GREEN}✓ Build setup completed on all machines${NC}"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Setup Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Next steps:"
echo "  1. Edit deploy_and_launch.sh and set NETID='${NETID}'"
echo "  2. Run: ./deploy_and_launch.sh"
echo ""

# Quick Start Guide

## First Time Setup (Do Once)

1. **Edit `setup_vms.sh` and set:**
   ```bash
   NETID="your_netid"           # Your actual NetID
   GIT_REPO_URL="your_git_url"  # Your git repository URL
   ```

2. **Run setup:**
   ```bash
   ./setup_vms.sh
   ```
   This clones your repo and builds on all 10 VMs.

3. **Configure deployment scripts:**
   ```bash
   # Edit these files and replace "your_netid" with your actual NetID
   nano deploy_and_launch.sh    # Set NETID
   nano simple_deploy.sh        # Set NETID
   nano kill_all.sh            # Set NETID
   nano ssh_to_vm.sh           # Set NETID
   nano test_3vms.sh           # Set NETID
   ```

   Or use sed (CAREFUL - double check your NetID first):
   ```bash
   sed -i '' 's/your_netid/YOUR_ACTUAL_NETID/g' *.sh
   ```

## Daily Development Workflow

### RECOMMENDED: Using tmux (Best Option!)
```bash
./tmux_launch.sh
```
- ✅ All 10 VMs in ONE terminal window
- ✅ Easy navigation with Ctrl+B + arrow keys
- ✅ Can type in all panes at once
- ✅ Session persists even if you disconnect
- Inside tmux, use synchronize-panes to type `join` in all non-introducer VMs

### Option 1: Auto-Launch Terminals (10 separate windows)
```bash
./deploy_and_launch.sh
```
- Opens 10 terminal windows automatically
- If it doesn't work, it prints manual commands
- In terminals: VM02-10 type `join`

### Option 2: Manual Commands (Most Reliable)
```bash
./manual_commands.sh
```
- Prints all SSH commands to run manually
- Copy-paste into your own terminal tabs
- Most control over the process

### Option 3: Quick Test (3 VMs only)
```bash
./test_3vms.sh
```
- Launches only VM01, VM02, VM03
- Faster for quick testing
- In terminals: VM02 and VM03 type `join`

### Option 4: Just Update Code (No Terminals)
```bash
./simple_deploy.sh
```
- Only pulls code and builds
- Doesn't open terminals
- Useful when terminals are already open

## Common Tasks

### Kill All Running Processes
```bash
./kill_all.sh
```

### SSH to Specific VM
```bash
./ssh_to_vm.sh 01   # SSH to VM01
./ssh_to_vm.sh 05   # SSH to VM05
```

### Manual SSH (if scripts don't work)
```bash
ssh your_netid@fa25-cs425-1601.cs.illinois.edu
cd ~/cs425_mp3
./build/main fa25-cs425-1601.cs.illinois.edu 12340
```

## Commands Available in Each VM Terminal

```
join                          - Join network (VM02-10 only)
leave                         - Leave network
list_mem                      - Show all members
list_self                     - Show self information
display_suspects              - Show suspected nodes
display_protocol              - Show current protocol
switch gossip suspect         - Switch to gossip with suspicion
switch gossip nosuspect       - Switch to gossip without suspicion
switch ping suspect           - Switch to ping-ack with suspicion
switch ping nosuspect         - Switch to ping-ack without suspicion
```

## Testing Workflow

1. **Start VMs:**
   ```bash
   ./test_3vms.sh  # or ./deploy_and_launch.sh for all 10
   ```

2. **In VM02 and VM03 terminals, join the network:**
   ```
   join
   ```

3. **Verify membership:**
   ```
   list_mem
   ```

4. **Test failure detection:**
   - In VM02 terminal: Press `Ctrl+C` to kill
   - In VM01 and VM03: Watch for failure detection
   - Check logs: `list_mem`

5. **Clean up:**
   ```bash
   ./kill_all.sh
   ```

## Troubleshooting

### Can't SSH to VMs
```bash
# Test connection
ssh rayans2@fa25-cs425-1601.cs.illinois.edu

# Setup SSH keys if prompted for password
ssh-keygen -t ed25519
ssh-copy-id rayans2@fa25-cs425-1601.cs.illinois.edu
```

### CMake Not Found on VMs
```bash
# The scripts now use 'make' directly instead of 'cmake --build'
# For initial setup, CMake is loaded via: module load cmake
```

### Build Fails
```bash
# SSH to VM and check build output
./ssh_to_vm.sh 01
cd ~/cs425_mp3
make -C build -j4
```

### Terminals Not Opening
- **Solution 1**: Use `./tmux_launch.sh` instead (recommended!)
- **Solution 2**: Use `./manual_commands.sh` to get manual commands
- **macOS**: Should work automatically with Terminal.app
- **Linux**: Install `gnome-terminal` or `xterm`

### Install tmux (Recommended)
```bash
# macOS
brew install tmux

# Ubuntu/Debian
sudo apt install tmux

# RHEL/CentOS
sudo yum install tmux
```

### Process Already Running
```bash
# Kill all first
./kill_all.sh

# Then try again
./deploy_and_launch.sh
```

## File Structure

```
cs425_mp3/
├── deploy_and_launch.sh   - Main deployment script (10 VMs)
├── test_3vms.sh          - Test with 3 VMs only
├── simple_deploy.sh      - Update code only (no terminals)
├── kill_all.sh          - Kill all processes
├── ssh_to_vm.sh         - SSH to specific VM
├── setup_vms.sh         - One-time setup script
├── DEPLOYMENT_GUIDE.md  - Detailed documentation
└── QUICK_START.md       - This file
```

## Tips

1. **Start Small**: Use `test_3vms.sh` first, then scale to 10 VMs
2. **Use tmux**: For persistent sessions that survive disconnects
3. **Check Logs**: Use your MP1 grep functionality
4. **Tag Working Versions**: `git tag -a milestone1 -m "Working"`
5. **Test Incrementally**: Don't write everything then test

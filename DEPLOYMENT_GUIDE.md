# MP3 Deployment Guide

This guide explains how to deploy and run your MP3 code on the CS VM cluster.

## Prerequisites

1. **SSH Access**: Ensure you can SSH into the VMs without entering password each time
   ```bash
   # Generate SSH key if you haven't already
   ssh-keygen -t ed25519

   # Copy to all VMs (do this for machines 01-10)
   ssh-copy-id your_netid@fa25-cs425-1601.cs.illinois.edu
   ```

2. **Git Setup**: Your code should be in a git repository accessible from the VMs

3. **Configure NetID**: Edit all scripts and replace `your_netid` with your actual NetID

## Scripts Provided

### 1. `deploy_and_launch.sh` - Full Deployment
**Purpose**: Pull code, build, and launch terminals for all 10 VMs

**What it does**:
- Pulls latest code from git on all VMs in parallel
- Builds the project on all VMs in parallel
- Opens 10 terminal windows, each SSH'd into a VM
- Automatically starts the program on each VM
  - VM01 runs as the introducer
  - VM02-10 connect to VM01 as introducer

**Usage**:
```bash
chmod +x deploy_and_launch.sh
./deploy_and_launch.sh
```

**Configuration**:
- `NETID`: Your NetID
- `GIT_BRANCH`: Git branch to deploy (default: main)
- `BASE_PORT`: Starting port number (default: 12340)
- `REMOTE_DIR`: Directory on VMs where code is located (default: ~/cs425_mp3)

### 2. `simple_deploy.sh` - Code Update Only
**Purpose**: Just pull code and build on all VMs (no terminals)

**Usage**:
```bash
chmod +x simple_deploy.sh
./simple_deploy.sh
```

Useful when you just want to update code without launching terminals.

### 3. `kill_all.sh` - Stop All Processes
**Purpose**: Kill all running `main` processes on all VMs

**Usage**:
```bash
chmod +x kill_all.sh
./kill_all.sh
```

Useful when you need to restart everything or clean up after testing.

### 4. `ssh_to_vm.sh` - Quick SSH Access
**Purpose**: Quickly SSH into a specific VM

**Usage**:
```bash
chmod +x ssh_to_vm.sh
./ssh_to_vm.sh 01  # SSH to VM01
./ssh_to_vm.sh 05  # SSH to VM05
```

## Workflow Example

### Initial Setup
1. Clone your repo on all VMs:
   ```bash
   # SSH to each VM and clone
   for i in {01..10}; do
     ssh your_netid@fa25-cs425-16$i.cs.illinois.edu "git clone <your-repo-url> ~/cs425_mp3"
   done
   ```

2. Configure scripts:
   ```bash
   # Edit all .sh files and replace "your_netid" with your actual NetID
   sed -i '' 's/your_netid/YOUR_ACTUAL_NETID/g' *.sh
   ```

### Regular Development Cycle
1. Make changes to code locally
2. Commit and push to git
3. Run deployment script:
   ```bash
   ./deploy_and_launch.sh
   ```
4. 10 terminal windows will open, program will start automatically
5. In the introducer terminal (VM01), just start typing commands
6. In other VMs (02-10), type `join` to join the network

### Testing
1. Use the commands in each terminal:
   ```
   join               # Join network (VM02-10 only)
   list_mem           # See all members
   list_self          # See own info
   leave              # Leave network
   ```

2. Test failures by killing processes:
   ```bash
   # In a VM terminal, press Ctrl+C to simulate failure
   ```

3. Clean up when done:
   ```bash
   ./kill_all.sh
   ```

## Port Configuration

By default, ports are assigned as:
- VM01: 12340 (introducer)
- VM02: 12341
- VM03: 12342
- ...
- VM10: 12349

You can modify `BASE_PORT` in the scripts to change this.

## Troubleshooting

### SSH Connection Issues
```bash
# Test SSH to a VM
ssh your_netid@fa25-cs425-1601.cs.illinois.edu

# If it asks for password, set up SSH keys
ssh-copy-id your_netid@fa25-cs425-1601.cs.illinois.edu
```

### Build Failures
```bash
# SSH to a specific VM to debug
./ssh_to_vm.sh 01

# Check build errors
cd ~/cs425_mp3
cmake --build build
```

### Git Issues
```bash
# If git pull fails, might need to setup git on VMs
for i in {01..10}; do
  ssh your_netid@fa25-cs425-16$i.cs.illinois.edu "git config --global user.name 'Your Name'"
  ssh your_netid@fa25-cs425-16$i.cs.illinois.edu "git config --global user.email 'your@email.com'"
done
```

### Terminal Not Opening
The script tries to detect your terminal emulator automatically. If it fails:
- **macOS**: Should work with Terminal.app automatically
- **Linux**: Install `gnome-terminal`, `konsole`, or `xterm`
- **Alternative**: Use `simple_deploy.sh` then SSH manually

### Processes Still Running
```bash
# Kill all processes
./kill_all.sh

# Or manually on specific VM
ssh your_netid@fa25-cs425-1601.cs.illinois.edu "pkill -9 -f './build/main'"
```

## Manual Alternative

If the scripts don't work for your setup, here's the manual process:

```bash
# Terminal 1 - VM01 (Introducer)
ssh your_netid@fa25-cs425-1601.cs.illinois.edu
cd ~/cs425_mp3
git pull
cmake --build build
./build/main fa25-cs425-1601.cs.illinois.edu 12340

# Terminal 2 - VM02
ssh your_netid@fa25-cs425-1602.cs.illinois.edu
cd ~/cs425_mp3
git pull
cmake --build build
./build/main fa25-cs425-1602.cs.illinois.edu 12341 fa25-cs425-1601.cs.illinois.edu 12340
# Type: join

# Repeat for VM03-10...
```

## Tips

1. **Use tmux/screen**: For persistent sessions that survive disconnects
   ```bash
   ssh your_netid@fa25-cs425-1601.cs.illinois.edu
   tmux new -s mp3
   cd ~/cs425_mp3
   ./build/main ...
   # Ctrl+B, D to detach
   # tmux attach -t mp3 to reattach
   ```

2. **Monitor logs**: Use MP1 grep functionality to query logs across machines

3. **Test incrementally**: Start with 3 machines, then scale to 10

4. **Keep backups**: Tag working versions in git
   ```bash
   git tag -a milestone1 -m "Working membership protocol"
   git push origin milestone1
   ```

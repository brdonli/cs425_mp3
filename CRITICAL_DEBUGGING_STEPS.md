# CRITICAL: Debugging Replication Issues

## Problem
Replica machines aren't receiving files - metadata folders are empty, no debug output on replicas.

## Most Likely Causes

### 1. **Nodes Haven't Joined the Network** (MOST COMMON)
The hash ring only knows about nodes that have explicitly joined via the `join` command.

### 2. **Wrong Host/Port Configuration**
Messages are being sent to wrong addresses or ports.

### 3. **Hash Ring Only Has One Node**
If only one node is in the ring, all replicas will be that same node.

## Step-by-Step Debugging

### Step 1: Check What the Debug Output Shows

When you run `create test.txt myfile.txt`, you should now see:

```
=== HASH RING STATUS ===
Total nodes in ring: X
Replicas for 'myfile.txt': Y
========================

=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - host1:port1
  - host2:port2
  - host3:port3
Serialized message size: XXXX bytes
  [SEND] Sending to host1:port1... ✅ SUCCESS / ❌ FAILED
  [SEND] Sending to host2:port2... ✅ SUCCESS / ❌ FAILED
  [SEND] Sending to host3:port3... ✅ SUCCESS / ❌ FAILED
================================
```

### Step 2: Interpret the Output

#### Scenario A: "Total nodes in ring: 1"
**Problem**: Only the current VM is in the hash ring!

**Solution**: Other VMs haven't joined the network

**Fix**:
```bash
# On EACH replica VM, in the running program:
join

# Verify with:
list_mem
# Should show multiple nodes
```

#### Scenario B: All replicas show the same host:port
**Example**:
```
Replicas determined by hash ring:
  - localhost:12345
  - localhost:12345
  - localhost:12345
```

**Problem**: Only one unique node in the ring

**Solution**: Same as Scenario A - other VMs need to join

#### Scenario C: Replicas show different ports but "❌ FAILED" when sending
**Example**:
```
  [SEND] Sending to localhost:12346... ❌ FAILED
  [SEND] Sending to localhost:12347... ❌ FAILED
```

**Problem**: Target VMs aren't running or aren't listening on those ports

**Solution**:
1. Check if those VMs are actually running
2. Check if they're using the correct ports
3. Check if they're on the same machine (localhost) or different machines

#### Scenario D: "✅ SUCCESS" but replicas show nothing
**Example**: Sender shows all SUCCESS, but replica VMs have no output

**Problem**: Messages sent successfully but not received

**Possible causes**:
1. Wrong IP/hostname (if VMs on different machines)
2. Firewall blocking UDP packets
3. drop_rate is dropping messages
4. Replica VM's handleIncoming() is not running

## Complete Test Procedure

### Setup (3 VMs on same machine):

**Terminal 1 (VM1 - Introducer):**
```bash
cd ~/cs425_mp3
./build/main localhost 12345
```

**Terminal 2 (VM2):**
```bash
cd ~/cs425_mp3
./build/main localhost 12346 localhost 12345
```
In the program:
```
join
```

**Terminal 3 (VM3):**
```bash
cd ~/cs425_mp3
./build/main localhost 12347 localhost 12345
```
In the program:
```
join
```

### Verify Network:

**On VM1:**
```
list_mem
```
Should show 3 nodes!

### Test File Creation:

**On VM2:**
```bash
# In your terminal (outside the program):
echo "Test content" > test.txt
```

```
# In the program:
create test.txt myfile.txt
```

### Expected Output on VM2:
```
=== HASH RING STATUS ===
Total nodes in ring: 3
Replicas for 'myfile.txt': 3
========================

=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - localhost:12345
  - localhost:12346
  - localhost:12347
Serialized message size: 1234 bytes
  [SEND] Sending to localhost:12345... ✅ SUCCESS
  [SKIP] localhost:12346 (already stored locally)
  [SEND] Sending to localhost:12347... ✅ SUCCESS
================================
```

### Expected Output on VM1 and VM3:
```
[FILE MSG] Received file message type: 100 (1250 bytes)

=== RECEIVED CREATE_REQUEST ===
Filename: myfile.txt
Data size: XXXX bytes
Client ID: XXXXX
✅ File created successfully: myfile.txt
================================
```

### Verify Files Exist:

**On all 3 VMs:**
```
store
```

Should show `myfile.txt` on all VMs that were selected as replicas.

## If Using VMs on Different Machines

### IMPORTANT: Don't use "localhost"!

**VM1 (on machine fa24-cs425-XXXX):**
```bash
./build/main fa24-cs425-XXXX.cs.illinois.edu 12345
```

**VM2 (on machine fa24-cs425-YYYY):**
```bash
./build/main fa24-cs425-YYYY.cs.illinois.edu 12345 fa24-cs425-XXXX.cs.illinois.edu 12345
```
```
join
```

**VM3 (on machine fa24-cs425-ZZZZ):**
```bash
./build/main fa24-cs425-ZZZZ.cs.illinois.edu 12345 fa24-cs425-XXXX.cs.illinois.edu 12345
```
```
join
```

## Common Mistakes

### ❌ Mistake 1: Not running `join` on each VM
```bash
# VM2 and VM3 need to explicitly join:
join
```

### ❌ Mistake 2: Using localhost for VMs on different machines
```bash
# WRONG (if VMs on different machines):
./build/main localhost 12345

# RIGHT:
./build/main fa24-cs425-XXXX.cs.illinois.edu 12345
```

### ❌ Mistake 3: Creating file before other VMs join
```bash
# WRONG order:
VM1: (start)
VM1: create test.txt file.txt  # Only VM1 in ring!
VM2: join  # Too late

# RIGHT order:
VM1: (start)
VM2: join
VM3: join
VM1: list_mem  # Verify 3 nodes
VM1: create test.txt file.txt  # Now has 3 replicas
```

### ❌ Mistake 4: File doesn't exist locally
```bash
# Make sure the local file exists FIRST:
echo "content" > test.txt
# Then:
create test.txt myfile.txt
```

## Checklist Before Creating Files

- [ ] All VMs are running
- [ ] All VMs have joined (except introducer)
- [ ] `list_mem` shows all VMs
- [ ] Local file exists (for create/append)
- [ ] Using correct hostnames (not localhost if on different machines)

## Still Not Working?

Share the EXACT output you see when you run `create`, including:
1. The "HASH RING STATUS" section
2. The "SENDING CREATE REQUESTS" section
3. Whether replicas show "[FILE MSG]" and "RECEIVED CREATE_REQUEST"

This will pinpoint the exact problem!

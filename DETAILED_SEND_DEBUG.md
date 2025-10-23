# Detailed Send Debug - Files Not Reaching Replicas

## Problem
- `ls` shows correct 3 replicas
- Hash ring has all nodes
- But replica machines have empty metadata/blocks folders
- Replicas show NO debug output

## Root Cause Possibilities

### 1. **Host/Port Mismatch**
The NodeId in the hash ring might have different host/port than the actual listening socket.

**Example**: NodeId has "localhost" but VMs are on different machines with real hostnames.

### 2. **Messages Sent to Wrong Address**
The messages are being sent, but to the wrong IP/port.

### 3. **Replicas Not Actually Running handleIncoming()**
The replica VMs might not be processing incoming messages.

## New Debug Output

When you run `create test.txt myfile.txt`, you'll now see:

```
=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - fa24-cs425-0101:12345
  - fa24-cs425-0102:12345
  - fa24-cs425-0103:12345
Serialized message size: 1234 bytes
  [SEND] Sending to fa24-cs425-0101:12345
         inet_pton result: 1 (1=success, 0=invalid, -1=error)
         Port (network byte order): 12345
         Result: ✅ SUCCESS
  [SEND] Sending to fa24-cs425-0102:12345
         inet_pton result: 1 (1=success, 0=invalid, -1=error)
         Port (network byte order): 12345
         Result: ✅ SUCCESS
================================
```

## Interpreting the Output

### Check 1: inet_pton result

**If inet_pton result: 0**
- Problem: Invalid IP address format
- The `replica.host` contains an invalid IP
- Example: "localhost" when it should be an actual hostname

**If inet_pton result: 1**
- Good: Address format is valid

### Check 2: Result

**If Result: ✅ SUCCESS**
- The `write_to_socket()` call succeeded
- Message was sent to the network

**If Result: ❌ FAILED**
- The `write_to_socket()` call failed
- Socket error or connection issue

### Check 3: Replica Shows Nothing

**If sender shows SUCCESS but replica shows NOTHING:**

This is your current situation! Possible causes:

#### A. Wrong hostname in NodeId

**On your VMs, check:**
```bash
# On the replica machine
hostname
# e.g., outputs: fa24-cs425-0102.cs.illinois.edu
```

**In your program on the sending machine:**
```
list_mem_ids
```

Does it show the FULL hostname or just "localhost"?

**Problem**: If it shows "localhost" but VMs are on different machines, messages won't reach them!

#### B. VMs Started with Wrong Host Parameter

**Check how you started each VM:**
```bash
# WRONG (if on fa24-cs425-0102):
./build/main localhost 12345

# RIGHT (if on fa24-cs425-0102):
./build/main fa24-cs425-0102.cs.illinois.edu 12345
```

#### C. Firewall Blocking UDP

Between CS VMs, this is unlikely but possible.

#### D. Replicas Not Processing Messages

Check on replica VMs - is handleIncoming() running? Does membership gossip work?

## Detailed Test Plan

### Step 1: Check Node Registration

**On the machine where you're creating the file, run:**
```
list_mem_ids
```

**Look at the output carefully:**
```
Member 1: localhost:12345 (Ring ID: ...)
Member 2: localhost:12346 (Ring ID: ...)
Member 3: localhost:12347 (Ring ID: ...)
```

**Question**: Are all showing "localhost" but they're actually on DIFFERENT machines?

### Step 2: Check How VMs Were Started

**For EACH replica that should receive the file:**

SSH to that machine and check:
```bash
ps aux | grep main
```

Look at the command line - does it show:
- `./build/main localhost 12345` (WRONG if on different machine)
- `./build/main fa24-cs425-XXXX.cs.illinois.edu 12345` (RIGHT)

### Step 3: Test with Explicit Hostnames

**Kill all VMs and restart with FULL hostnames:**

**VM1 (on fa24-cs425-0101):**
```bash
./build/main fa24-cs425-0101.cs.illinois.edu 12345
```

**VM2 (on fa24-cs425-0102):**
```bash
./build/main fa24-cs425-0102.cs.illinois.edu 12345 fa24-cs425-0101.cs.illinois.edu 12345
```
In program:
```
join
```

**VM3 (on fa24-cs425-0103):**
```bash
./build/main fa24-cs425-0103.cs.illinois.edu 12345 fa24-cs425-0101.cs.illinois.edu 12345
```
In program:
```
join
```

**Verify on VM1:**
```
list_mem_ids
```

Should show FULL hostnames like:
```
Member 1: fa24-cs425-0101.cs.illinois.edu:12345 (Ring ID: ...)
Member 2: fa24-cs425-0102.cs.illinois.edu:12345 (Ring ID: ...)
Member 3: fa24-cs425-0103.cs.illinois.edu:12345 (Ring ID: ...)
```

**Now test create:**
```bash
# On VM1 (outside program):
echo "Test" > test.txt
```
In program:
```
create test.txt myfile.txt
```

### Step 4: Watch for the New Debug Output

**On the sender (VM1), you should see:**
```
=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - fa24-cs425-0101.cs.illinois.edu:12345
  - fa24-cs425-0102.cs.illinois.edu:12345
  - fa24-cs425-0103.cs.illinois.edu:12345
Serialized message size: 1234 bytes
  [SEND] Sending to fa24-cs425-0101.cs.illinois.edu:12345
         inet_pton result: 1
         Port (network byte order): 12345
         Result: ✅ SUCCESS
```

**On replicas (VM2, VM3), you should see:**
```
[FILE MSG] Received file message type: 100 (1250 bytes)

=== RECEIVED CREATE_REQUEST ===
Filename: myfile.txt
Data size: 1234 bytes
Client ID: 12345
✅ File created successfully: myfile.txt
================================
```

## If inet_pton Returns 0

This means the hostname is invalid for inet_pton (it expects an IP address).

**This is actually expected for hostnames!** We need to resolve them first.

### Quick Fix: Check if inet_pton is Failing

If you see `inet_pton result: 0`, that means the NodeId contains a hostname (like "fa24-cs425-0101") instead of an IP address.

We need to use `getaddrinfo()` to resolve hostnames to IPs.

## What to Share

After running `create` with the new debug output, please share:

1. **The complete "SENDING CREATE REQUESTS" section** - especially:
   - What hostnames/IPs are shown
   - inet_pton results
   - SUCCESS or FAILED results

2. **Output from `list_mem_ids`** - to see how nodes are registered

3. **How you started each VM** - exact command line

4. **Are VMs on same machine or different machines?**

This will pinpoint whether it's a hostname resolution issue, wrong addresses, or something else!

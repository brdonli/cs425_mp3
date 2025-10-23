# Debug Output Added for File Replication

## Overview

Added comprehensive debug logging to diagnose why files aren't appearing on replicas. The debug output will show:

1. **When sending CREATE_REQUEST**: Which replicas are being targeted and if the send succeeded
2. **When receiving messages**: What type of message was received
3. **When handling CREATE_REQUEST**: If the file was successfully stored

## What Was Added

### 1. Debug Output in `createFile()` (Sender Side)

Location: `src/file_operations_handler.cpp` lines 168-213

**Output shows:**
- The file being created
- All replicas determined by the hash ring
- Which replicas are being sent to (or skipped)
- Success/failure of each send operation
- Serialized message size

**Example Output:**
```
=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - localhost:12346
  - localhost:12347
  - localhost:12348
Serialized message size: 1234 bytes
  [SEND] Sending to localhost:12346... ✅ SUCCESS
  [SEND] Sending to localhost:12347... ✅ SUCCESS
  [SEND] Sending to localhost:12348... ✅ SUCCESS
================================
```

### 2. Debug Output in `handleIncoming()` (Receiver Side - Message Routing)

Location: `src/node.cpp` lines 68-75

**Output shows:**
- When a file message is received
- The message type number
- The message size
- If file handler is null (error condition)

**Example Output:**
```
[FILE MSG] Received file message type: 100 (1250 bytes)
```

### 3. Debug Output in `handleCreateRequest()` (Receiver Side - File Storage)

Location: `src/file_operations_handler.cpp` lines 376-395

**Output shows:**
- When CREATE_REQUEST is received
- The filename and data size
- Client ID
- Success or failure of file storage

**Example Output:**
```
=== RECEIVED CREATE_REQUEST ===
Filename: myfile.txt
Data size: 1234 bytes
Client ID: 12345
✅ File created successfully: myfile.txt
================================
```

## How to Use This for Debugging

### Test Scenario: Create a File from VM1

**VM1 (Sender):**
```bash
echo "Hello World" > test.txt
./build/main localhost 12345
```

In the program:
```
join  # if not introducer
create test.txt myfile.txt
```

**Expected Output on VM1:**
```
=== SENDING CREATE REQUESTS ===
File: myfile.txt
Replicas determined by hash ring:
  - localhost:12346
  - localhost:12347
  - localhost:12348
Serialized message size: 1234 bytes
  [SEND] Sending to localhost:12346... ✅ SUCCESS
  [SEND] Sending to localhost:12347... ✅ SUCCESS
  [SEND] Sending to localhost:12348... ✅ SUCCESS
================================

File created successfully and sent to 3 replica(s): myfile.txt
```

**VM2, VM3, VM4 (Replicas) - Expected Output:**
```
[FILE MSG] Received file message type: 100 (1250 bytes)

=== RECEIVED CREATE_REQUEST ===
Filename: myfile.txt
Data size: 1234 bytes
Client ID: 12345
✅ File created successfully: myfile.txt
================================
```

## Diagnostic Scenarios

### Scenario 1: Messages Not Being Sent
If you see:
```
  [SEND] Sending to localhost:12346... ❌ FAILED
```

**Problem**: sendFileMessage() is failing
**Check**:
- Is the target VM running?
- Is the port correct?
- Are there network issues?

### Scenario 2: Messages Not Being Received
If sender shows SUCCESS but replica shows nothing:

**Problem**: Messages are being sent but not received
**Check**:
- Is the replica VM running and listening?
- Check `handleIncoming()` is running
- Check drop_rate isn't dropping messages
- Check UDP socket is working

### Scenario 3: Messages Received but Not Routed
If replica shows:
```
[FILE MSG] Received file message type: 100 (1250 bytes)
```
But NO "RECEIVED CREATE_REQUEST" output:

**Problem**: Message routing to file handler is broken
**Check**:
- Is file_handler_ null? (error message would show)
- Is handleFileMessage() being called?
- Is the message type correctly deserialized?

### Scenario 4: CREATE_REQUEST Received but File Not Created
If replica shows:
```
=== RECEIVED CREATE_REQUEST ===
Filename: myfile.txt
Data size: 1234 bytes
Client ID: 12345
❌ File creation failed (may already exist): myfile.txt
================================
```

**Problem**: File storage is failing
**Check**:
- Does the file already exist? (expected behavior)
- Is the storage directory writable?
- Is file_store_ working correctly?

### Scenario 5: Wrong Replicas
If the sender shows:
```
Replicas determined by hash ring:
  - localhost:12345
  - localhost:12345
  - localhost:12345
```
(All the same!)

**Problem**: Hash ring is broken or only one node in the ring
**Check**:
- Have all VMs joined the network?
- Is the membership list populated?
- Is the hash ring being updated correctly?

## Files Modified

1. **`src/file_operations_handler.cpp`**:
   - Lines 168-213: Added debug output for sending CREATE_REQUEST
   - Lines 376-395: Added debug output for receiving CREATE_REQUEST

2. **`src/node.cpp`**:
   - Lines 68-75: Added debug output for file message routing

## Build and Test

```bash
make clean
make -j4
./build/main localhost 12345
```

## What to Look For

### On the SENDER VM:
1. ✅ "SENDING CREATE REQUESTS" section appears
2. ✅ All target replicas are listed
3. ✅ All sends show "SUCCESS"
4. ✅ "File created successfully and sent to N replica(s)" message

### On REPLICA VMs:
1. ✅ "[FILE MSG] Received file message type: 100" appears
2. ✅ "RECEIVED CREATE_REQUEST" section appears
3. ✅ "File created successfully" message appears
4. ✅ `store` command shows the file

## Common Issues and Solutions

### Issue: No debug output at all on sender
**Solution**: Make sure you're in the right VM and typed the command correctly

### Issue: "FAILED" when sending
**Solution**: Check target VMs are running and ports are correct

### Issue: Sender shows SUCCESS but replica shows nothing
**Solution**: Check replica is running `handleIncoming()` loop and not blocked

### Issue: "[FILE MSG]" appears but no "RECEIVED CREATE_REQUEST"
**Solution**: Check handleFileMessage() routing and deserialization

### Issue: "File creation failed"
**Solution**: Check if file already exists or storage directory issues

## Next Steps

1. Start 3 VMs
2. Run `create` command on one VM
3. Watch the debug output on all VMs
4. Identify where the process breaks
5. Use the diagnostic scenarios above to fix the issue

The debug output will pinpoint exactly where the replication process is failing!

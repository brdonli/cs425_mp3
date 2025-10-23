# HyDFS File Operations - Usage Guide

This guide explains how to test the newly implemented HyDFS file operations.

## Available Commands

### File Operations

1. **create** `<localfilename> <hydfsfilename>`
   - Creates a new file in HyDFS from a local file
   - Only the first create succeeds; subsequent creates to the same filename will fail
   - File is automatically replicated to 3 successor nodes on the hash ring

2. **get** `<hydfsfilename> <localfilename>`
   - Retrieves a file from HyDFS and saves it locally
   - Satisfies "read-my-writes" consistency guarantee

3. **append** `<localfilename> <hydfsfilename>`
   - Appends contents of local file to existing HyDFS file
   - Creates a new block with unique block_id, client_id, sequence_num, and timestamp
   - Coordinator replicates the block to all replicas

4. **merge** `<hydfsfilename>`
   - Ensures all replicas of a file are identical
   - Reconciles block lists across replicas while preserving per-client ordering
   - Can be called explicitly or run periodically in background

5. **ls** `<hydfsfilename>`
   - Lists all VMs storing the file with their ring IDs and file ID

6. **store**
   - Lists all files stored on the current node with their file IDs

7. **getfromreplica** `<vmaddress> <hydfsfilename> <localfilename>`
   - Gets file from a specific replica node
   - Format for vmaddress: `host:port`

## Testing Instructions

### Step 1: Start Multiple Nodes

In separate terminals, start 3 or more nodes:

```bash
# Terminal 1 - Introducer (VM1)
./build/main localhost 12345

# Terminal 2 - VM2
./build/main localhost 12346 localhost 12345

# Terminal 3 - VM3
./build/main localhost 12347 localhost 12345

# Terminal 4 - VM4 (optional, for testing with more nodes)
./build/main localhost 12348 localhost 12345
```

### Step 2: Join the Network

In each non-introducer terminal:
```
join
```

Verify membership with:
```
list_mem_ids
```

This shows all nodes with their ring IDs.

### Step 3: Create Test Files

First, create some local test files to upload:

```bash
# In your shell (outside the program)
echo "Hello, this is my first file!" > test1.txt
echo "This is test file 2 with different content." > test2.txt
echo "Append data goes here" > append1.txt
```

### Step 4: Test File Operations

#### Test CREATE

In VM2's terminal:
```
create test1.txt file1.txt
```

Verify it was stored using `ls`:
```
ls file1.txt
```

This should show 3 replicas (or fewer if you have < 3 nodes).

Check local storage on the current node:
```
store
```

#### Test GET

In VM3's terminal (different from where you created):
```
get file1.txt retrieved_file1.txt
```

Then verify the contents match:
```bash
# In shell
cat retrieved_file1.txt
```

#### Test APPEND

In VM2's terminal:
```
append append1.txt file1.txt
```

Then retrieve and verify:
```
get file1.txt file1_after_append.txt
```

```bash
# In shell
cat file1_after_append.txt
# Should contain original + appended content
```

#### Test Multiple Concurrent Appends

In VM2:
```
append append1.txt file1.txt
```

Simultaneously in VM3:
```
append test2.txt file1.txt
```

Then run merge to reconcile:
```
merge file1.txt
```

#### Test GET from Specific Replica

```
getfromreplica localhost:12345 file1.txt file1_from_vm1.txt
```

### Step 5: Verify File Locations and Storage

List where a file is stored:
```
ls file1.txt
```

Output example:
```
File: file1.txt
File ID: 12345678901234567890
Stored on 3 replicas:
  - localhost:12345 (ring ID: 987654321)
  - localhost:12346 (ring ID: 123456789)
  - localhost:12347 (ring ID: 567890123)
```

List all files on current node:
```
store
```

## Testing Consistency Guarantees

### Read-My-Writes Test

1. In VM2, create and append:
   ```
   create test1.txt myfile.txt
   append append1.txt myfile.txt
   ```

2. Immediately get (before merge):
   ```
   get myfile.txt check.txt
   ```

3. Verify that your append is visible in check.txt (read-my-writes guarantee)

### Per-Client Ordering Test

1. In VM2, do multiple appends:
   ```
   append test1.txt ordered_file.txt
   append test2.txt ordered_file.txt
   append append1.txt ordered_file.txt
   ```

2. Get the file and verify blocks appear in the order you appended them

3. Try appending from VM3 as well:
   ```
   append test1.txt ordered_file.txt
   ```

4. Run merge and verify per-client ordering is preserved:
   ```
   merge ordered_file.txt
   get ordered_file.txt final.txt
   ```

## Architecture Overview

### Components Implemented

1. **FileBlock** (`file_block.hpp/cpp`)
   - Represents individual append operations
   - Contains: block_id, client_id, sequence_num, timestamp, data

2. **FileMetadata** (`file_metadata.hpp/cpp`)
   - Tracks file information
   - Contains: filename, file_id, total_size, block_ids list, version, timestamps

3. **FileStore** (`file_store.hpp/cpp`)
   - Local storage management
   - Methods: createFile, appendBlock, getFile, mergeFile, etc.

4. **ConsistentHashRing** (`consistent_hash_ring.hpp/cpp`)
   - Maps nodes and files to ring positions
   - Determines which nodes store which files (first n successors)

5. **ClientTracker** (`client_tracker.hpp/cpp`)
   - Tracks client append operations
   - Enables read-my-writes consistency

6. **FileOperationsHandler** (`file_operations_handler.hpp/cpp`)
   - Handles all file operation requests
   - Coordinates replication and communication

7. **File Message Types** (`file_metadata.hpp`, `file_message.cpp`)
   - Complete serialization/deserialization for all message types
   - Supports: CREATE, GET, APPEND, MERGE, LS, LISTSTORE, replication messages

### File Storage Location

Files are stored in directories named: `./hydfs_storage_<host>_<port>`

For example:
- VM1 (localhost:12345): `./hydfs_storage_localhost_12345/`
- VM2 (localhost:12346): `./hydfs_storage_localhost_12346/`

Each directory contains:
- `metadata/` - File metadata
- `blocks/` - Individual file blocks

## Known Limitations & TODO

1. **Merge Implementation**: The merge operation currently responds with success but doesn't fully implement the coordinator collecting blocks from all replicas and distributing the merged result. This is partially implemented in the handler.

2. **Failure Recovery**: Node failure handling and file re-replication is not yet implemented.

3. **Disk Persistence**: The persist methods in FileStore are currently no-ops. Files are kept in memory but not persisted to disk.

4. **Response Handling**: File operation responses are sent but not fully processed on the client side. This is a network communication enhancement.

5. **Multi-append Support**: The `multiappend` command mentioned in the spec is not yet implemented.

## Troubleshooting

### "No replicas found"
- Make sure at least one node has joined the network
- Check that nodes are in the ring with `list_mem_ids`

### "File already exists"
- You can only create a file once
- Use `append` to add more data to existing files

### "File not found"
- Make sure the file was created successfully first
- Check with `ls <filename>` to see which nodes should have it
- Verify with `store` on those nodes

### Network issues
- Ensure all nodes are using different ports
- Check that the introducer is running before other nodes join

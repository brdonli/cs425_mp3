# MP3 Data Structures Summary

This document summarizes the data structures added for MP3 (HyDFS).

## Files Added

### Header Files (include/)
1. **file_block.hpp** - Represents individual file blocks (appends)
2. **file_metadata.hpp** - Metadata for files in HyDFS
3. **consistent_hash_ring.hpp** - Consistent hashing implementation
4. **file_store.hpp** - Local storage management for files and blocks
5. **client_tracker.hpp** - Tracks client operations for read-my-writes consistency

### Implementation Files (src/)
1. **file_block.cpp**
2. **file_metadata.cpp**
3. **consistent_hash_ring.cpp**
4. **file_store.cpp**
5. **client_tracker.cpp**

## Data Structure Overview

### 1. FileBlock
**Purpose**: Represents a single block of data (one append operation)

**Key Fields**:
- `block_id` - Unique identifier for the block
- `client_id` - Which client created this block
- `sequence_num` - Order within client's appends
- `timestamp` - When the append occurred
- `data` - The actual data
- `size` - Size of the data

**Key Methods**:
- `generateBlockId()` - Creates unique block ID from client_id + timestamp + sequence
- `serialize()` / `deserialize()` - For network transmission

**Why**: Storing appends as blocks allows efficient merging and preserves per-client ordering.

---

### 2. FileMetadata
**Purpose**: Tracks metadata for a file in HyDFS

**Key Fields**:
- `hydfs_filename` - Name in HyDFS
- `file_id` - Hash of filename
- `total_size` - Total size of all blocks
- `block_ids` - Ordered list of blocks comprising the file
- `version` - For merge conflict resolution
- `created_timestamp` / `last_modified_timestamp`

**Key Methods**:
- `generateFileId()` - Hash filename to get file ID
- `serialize()` / `deserialize()` - For network transmission

**Why**: Separates metadata from data; allows quick lookups and version tracking.

---

### 3. ConsistentHashRing
**Purpose**: Maps nodes and files to positions on a consistent hash ring

**Key Methods**:
- `addNode()` / `removeNode()` - Manage nodes in the ring
- `getNodePosition()` - Get ring position for a node
- `getFilePosition()` - Get ring position for a file
- `getSuccessors(position, n)` - Get first n successor nodes
- `getFileReplicas(filename, n)` - Get nodes that should store a file
- `getAllNodes()` - Get all nodes sorted by ring position

**Internal**:
- `std::map<uint64_t, NodeId> ring` - Sorted map of positions to nodes
- Uses simple hash function (can upgrade to SHA-256/MD5 later)

**Why**: Core of HyDFS - determines where files are stored and how to route requests.

---

### 4. FileStore
**Purpose**: Manages local storage of files and blocks on this node

**Key Methods**:
- `createFile()` - Create new file with initial data
- `appendBlock()` - Add a block to existing file
- `getFile()` - Get complete file contents (assembled from blocks)
- `getFileBlocks()` - Get all blocks for a file
- `hasFile()` - Check if file exists locally
- `listFiles()` - List all files stored on this node
- `mergeFile()` - Replace file's blocks with merged version
- `deleteFile()` - Remove file and all blocks
- `clearAllFiles()` - Clear everything (for node rejoin)
- `storeFile()` - Store complete file (for replication)

**Internal**:
- `std::unordered_map<string, FileMetadata> files` - filename -> metadata
- `std::unordered_map<uint64_t, FileBlock> blocks` - block_id -> block
- `storage_dir` - Directory for persistence (currently memory-only)

**Why**: Encapsulates all local file operations; separates storage from networking.

---

### 5. ClientTracker
**Purpose**: Tracks which blocks each client has appended for read-my-writes consistency

**Key Methods**:
- `recordAppend()` - Record successful append by client
- `getClientAppends()` - Get all blocks a client appended to a file
- `satisfiesReadMyWrites()` - Check if file version includes client's appends
- `clearClient()` / `clearFile()` - Cleanup methods

**Internal**:
- `client_id -> (filename -> [block_ids])` mapping

**Why**: Ensures read-my-writes guarantee - when a client reads, it must see its own appends.

---

## How These Components Work Together

### File Creation Flow:
1. Client issues `create localfile hydfsfile`
2. Hash filename using `ConsistentHashRing::getFilePosition()`
3. Find replicas using `ConsistentHashRing::getFileReplicas(filename, 3)`
4. First replica = coordinator
5. Coordinator uses `FileStore::createFile()` to store locally
6. Coordinator replicates to other 2 nodes
7. `ClientTracker::recordAppend()` tracks the creation

### Append Flow:
1. Client issues `append localfile hydfsfile`
2. Use ring to find file's replicas
3. Send append to coordinator (first replica)
4. Coordinator creates `FileBlock` with unique block_id
5. Coordinator calls `FileStore::appendBlock()`
6. Coordinator replicates block to other replicas
7. All replicas call `FileStore::appendBlock()`
8. `ClientTracker::recordAppend()` on all replicas

### Get Flow:
1. Client issues `get hydfsfile localfile`
2. Use ring to find a replica (any of the 3)
3. Check `ClientTracker::satisfiesReadMyWrites()` for this client
4. If satisfied, call `FileStore::getFile()` to assemble file from blocks
5. Return file to client

### Merge Flow:
1. Coordinator collects all blocks from all replicas using `FileStore::getFileBlocks()`
2. Sort blocks by (client_id, sequence_num) to preserve per-client ordering
3. Resolve conflicts based on timestamps
4. Call `FileStore::mergeFile()` on all replicas with canonical block order
5. All replicas now have identical files

### Failure Handling:
1. MP2's membership protocol detects failure
2. `ConsistentHashRing::removeNode()` removes failed node
3. For each file, check if we lost a replica
4. If yes, find new successor using `getSuccessors()`
5. Replicate file to new node using `FileStore::storeFile()`

### Node Join:
1. New node joins via MP2 membership
2. `ConsistentHashRing::addNode()` adds node to ring
3. For each file, recalculate replicas using `getFileReplicas()`
4. If new node should store a file, replicate to it
5. If old node should no longer store a file, delete it

---

## Next Steps for Implementation

### Week 1: Ring Integration
- [ ] Integrate `ConsistentHashRing` into `Node` class
- [ ] Update membership callbacks to add/remove nodes from ring
- [ ] Implement `list_mem_ids` to show ring positions
- [ ] Test ring behavior with joins/leaves

### Week 2: Basic File Operations
- [ ] Integrate `FileStore` into `Node` class
- [ ] Implement `create` command (single replica for now)
- [ ] Implement `get` command
- [ ] Implement `ls` and `liststore` commands
- [ ] Test local file operations

### Week 3: Replication
- [ ] Extend `MessageType` enum for file operations
- [ ] Implement coordinator-based replication
- [ ] Implement `append` with block creation
- [ ] Integrate `ClientTracker` for read-my-writes
- [ ] Test concurrent appends

### Week 4: Failure Handling & Merge
- [ ] Implement re-replication on node failure
- [ ] Implement rebalancing on node join
- [ ] Implement `merge` operation
- [ ] Implement `getfromreplica` and `multiappend`
- [ ] Test all failure scenarios

### Week 5: Testing & Measurements
- [ ] Run performance measurements from spec
- [ ] Write unit tests
- [ ] Use MP1 for debugging/verification
- [ ] Write report

---

## Important Design Notes

1. **Replication Factor**: n=3 (tolerates 2 simultaneous failures)

2. **Block-based Storage**: Each append creates a new block, making merge efficient

3. **Coordinator Pattern**: First successor on ring coordinates writes for each file

4. **Read-my-writes**: ClientTracker ensures clients see their own writes

5. **Eventual Consistency**: Merge reconciles block lists across replicas

6. **Per-client Ordering**: sequence_num in FileBlock maintains order

7. **Thread Safety**: All data structures use shared_mutex for concurrent access

8. **Persistence**: Currently in-memory; can add disk persistence later if needed

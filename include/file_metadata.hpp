#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "file_block.hpp"

/**
 * Metadata for a file stored in HyDFS
 * Tracks file information and ordered list of blocks
 */
struct FileMetadata {
  std::string hydfs_filename;           // name of file in HyDFS
  uint64_t file_id;                     // hash of filename
  size_t total_size;                    // total size of all blocks
  std::vector<uint64_t> block_ids;      // ordered list of block IDs
  uint32_t version;                     // version for merge conflict resolution
  uint64_t created_timestamp;           // when file was created
  uint64_t last_modified_timestamp;     // last modification time

  // Serialize metadata to buffer
  size_t serialize(char* buffer, size_t buffer_size) const;

  // Deserialize metadata from buffer
  static FileMetadata deserialize(const char* buffer, size_t buffer_size);

  // Generate file ID from filename
  static uint64_t generateFileId(const std::string& filename);
};

/**
 * Message types for HyDFS file operations
 * NOTE: Values start at 100 to distinguish from membership MessageType (0-5)
 */
enum class FileMessageType : uint8_t {
  // Core file operations
  CREATE_REQUEST = 100,     // Request to create a new file
  CREATE_RESPONSE,          // Response to create request
  GET_REQUEST,              // Request to fetch a file
  GET_RESPONSE,             // Response with file data
  APPEND_REQUEST,           // Request to append to a file
  APPEND_RESPONSE,          // Response to append request
  MERGE_REQUEST,            // Request to merge file replicas
  MERGE_RESPONSE,           // Response to merge request

  // Replication operations
  REPLICATE_FILE,           // Replicate file to a node
  REPLICATE_BLOCK,          // Replicate a block to a node
  REPLICATE_ACK,            // Acknowledgement of replication

  // Query operations
  LS_REQUEST,               // Request to list file locations
  LS_RESPONSE,              // Response with file locations
  LISTSTORE_REQUEST,        // Request to list files on a node
  LISTSTORE_RESPONSE,       // Response with stored files
  FILE_EXISTS_REQUEST,      // Check if a file exists at a replica
  FILE_EXISTS_RESPONSE,     // Response with existence status

  // Merge coordination
  COLLECT_BLOCKS_REQUEST,   // Coordinator requests blocks from replica
  COLLECT_BLOCKS_RESPONSE,  // Replica responds with blocks
  MERGE_UPDATE,             // Coordinator sends merged block list
  MERGE_UPDATE_ACK,         // Acknowledgement of merge update

  // Failure handling
  TRANSFER_FILES,           // Transfer files to new replica during failure recovery
  DELETE_FILE,              // Delete a file from a node

  // Error responses
  ERROR_FILE_EXISTS,        // File already exists (create failed)
  ERROR_FILE_NOT_FOUND,     // File not found
  ERROR_REPLICA_UNAVAILABLE // Replica not available
};

/**
 * Request to create a new file in HyDFS
 */
struct CreateFileRequest {
  std::string hydfs_filename;
  std::string local_filename;
  uint64_t client_id;
  std::vector<char> data;
  size_t data_size;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static CreateFileRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response to create file request
 */
struct CreateFileResponse {
  bool success;
  std::string error_message;
  uint64_t file_id;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static CreateFileResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to get a file from HyDFS
 */
struct GetFileRequest {
  std::string hydfs_filename;
  std::string local_filename;
  uint64_t client_id;
  uint32_t last_known_sequence;  // For read-my-writes consistency

  size_t serialize(char* buffer, size_t buffer_size) const;
  static GetFileRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response to get file request
 */
struct GetFileResponse {
  bool success;
  std::string error_message;
  FileMetadata metadata;
  std::vector<FileBlock> blocks;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static GetFileResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to append to a file in HyDFS
 */
struct AppendFileRequest {
  std::string hydfs_filename;
  std::string local_filename;
  uint64_t client_id;
  uint32_t sequence_num;
  std::vector<char> data;
  size_t data_size;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static AppendFileRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response to append file request
 */
struct AppendFileResponse {
  bool success;
  std::string error_message;
  uint64_t block_id;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static AppendFileResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to merge file replicas
 */
struct MergeFileRequest {
  std::string hydfs_filename;
  bool is_coordinator;  // True if this node is the coordinator

  size_t serialize(char* buffer, size_t buffer_size) const;
  static MergeFileRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response to merge file request
 */
struct MergeFileResponse {
  bool success;
  std::string error_message;
  uint32_t new_version;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static MergeFileResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to list VMs storing a file
 */
struct LsFileRequest {
  std::string hydfs_filename;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static LsFileRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response with file locations
 */
struct LsFileResponse {
  bool success;
  std::string error_message;
  uint64_t file_id;
  std::vector<std::string> vm_addresses;
  std::vector<uint64_t> ring_ids;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static LsFileResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to list files stored on current node
 */
struct ListStoreRequest {
  // Empty - just a request to list local files

  size_t serialize(char* buffer, size_t buffer_size) const;
  static ListStoreRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response with list of stored files
 */
struct ListStoreResponse {
  std::vector<std::string> filenames;
  std::vector<uint64_t> file_ids;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static ListStoreResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request to check if a file exists at a replica
 */
struct FileExistsRequest {
  std::string hydfs_filename;
  std::string requester_id;  // ID of node making the request

  size_t serialize(char* buffer, size_t buffer_size) const;
  static FileExistsRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response indicating if a file exists
 */
struct FileExistsResponse {
  std::string hydfs_filename;
  bool exists;
  uint64_t file_id;        // Only valid if exists == true
  size_t file_size;        // Only valid if exists == true
  uint32_t version;        // Only valid if exists == true

  size_t serialize(char* buffer, size_t buffer_size) const;
  static FileExistsResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Message to replicate a file block to a replica
 */
struct ReplicateBlockMessage {
  std::string hydfs_filename;
  FileBlock block;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static ReplicateBlockMessage deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Request for replica to send its blocks during merge
 */
struct CollectBlocksRequest {
  std::string hydfs_filename;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static CollectBlocksRequest deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Response with blocks from replica during merge
 */
struct CollectBlocksResponse {
  std::string hydfs_filename;
  std::vector<FileBlock> blocks;
  uint32_t version;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static CollectBlocksResponse deserialize(const char* buffer, size_t buffer_size);
};

/**
 * Coordinator sends merged block list to replicas
 */
struct MergeUpdateMessage {
  std::string hydfs_filename;
  std::vector<uint64_t> merged_block_ids;
  uint32_t new_version;

  size_t serialize(char* buffer, size_t buffer_size) const;
  static MergeUpdateMessage deserialize(const char* buffer, size_t buffer_size);
};

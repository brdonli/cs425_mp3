#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

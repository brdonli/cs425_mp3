#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "file_block.hpp"
#include "file_metadata.hpp"

/**
 * Local file storage for HyDFS
 * Manages files and blocks stored on this node
 */
class FileStore {
 public:
  explicit FileStore(const std::string& storage_dir);

  // Create a new file with initial data
  bool createFile(const std::string& filename, const std::vector<char>& data,
                  const std::string& client_id);

  // Append a block to an existing file
  bool appendBlock(const std::string& filename, const FileBlock& block);

  // Get entire file contents (assembled from blocks)
  std::vector<char> getFile(const std::string& filename) const;

  // Get all blocks for a file (in order)
  std::vector<FileBlock> getFileBlocks(const std::string& filename) const;

  // Get metadata for a file
  FileMetadata getFileMetadata(const std::string& filename) const;

  // Check if file exists
  bool hasFile(const std::string& filename) const;

  // Get list of all files stored locally
  std::vector<std::string> listFiles() const;

  // Merge file blocks from multiple replicas
  bool mergeFile(const std::string& filename, std::vector<FileBlock>& all_blocks);

  // Delete a file and all its blocks
  bool deleteFile(const std::string& filename);

  // Delete all files (used when node rejoins)
  void clearAllFiles();

  // Store a complete file (metadata + blocks) - used for replication
  bool storeFile(const FileMetadata& metadata, const std::vector<FileBlock>& blocks);

 private:
  std::string storage_dir;                                    // directory for file storage
  std::unordered_map<std::string, FileMetadata> files;        // filename -> metadata
  std::unordered_map<uint64_t, FileBlock> blocks;             // block_id -> block
  mutable std::shared_mutex mtx;                              // thread safety

  // Helper: persist metadata to disk
  void persistMetadata(const std::string& filename);

  // Helper: persist block to disk
  void persistBlock(uint64_t block_id);

  // Helper: load metadata from disk
  void loadMetadata(const std::string& filename);

  // Helper: load block from disk
  void loadBlock(uint64_t block_id);

  // Helper: get file path for metadata
  std::string getMetadataPath(const std::string& filename) const;

  // Helper: get file path for block
  std::string getBlockPath(uint64_t block_id) const;
};

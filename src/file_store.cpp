#include "file_store.hpp"

#include <chrono>
#include <iostream>
#include <mutex>

FileStore::FileStore(const std::string& storage_dir) : storage_dir(storage_dir) {
  // In-memory only storage - no disk persistence
  std::cout << "[FILE_STORE] Initialized in-memory storage for " << storage_dir << std::endl;
}

bool FileStore::createFile(const std::string& filename, const std::vector<char>& data,
                           const std::string& client_id) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  std::cout << "[FILE_STORE] createFile called: " << filename << " (" << data.size() << " bytes)" << std::endl;

  // Check if file already exists
  if (files.find(filename) != files.end()) {
    std::cout << "[FILE_STORE] File already exists: " << filename << std::endl;
    return false;  // File already exists
  }

  // Create metadata
  FileMetadata metadata;
  metadata.hydfs_filename = filename;
  metadata.file_id = FileMetadata::generateFileId(filename);
  metadata.total_size = data.size();
  metadata.version = 1;

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  metadata.created_timestamp = timestamp;
  metadata.last_modified_timestamp = timestamp;

  // Create initial block if data is not empty
  if (!data.empty()) {
    FileBlock block;
    block.client_id = client_id;
    block.sequence_num = 0;
    block.timestamp = timestamp;
    block.data = data;
    block.size = data.size();
    block.block_id = FileBlock::generateBlockId(client_id, timestamp, 0);

    metadata.block_ids.push_back(block.block_id);
    blocks[block.block_id] = block;
  }

  files[filename] = metadata;

  std::cout << "[FILE_STORE] File created successfully in memory: " << filename << std::endl;
  return true;
}

bool FileStore::appendBlock(const std::string& filename, const FileBlock& block) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  std::cout << "[FILE_STORE] appendBlock called: " << filename << " (block " << block.block_id << ")" << std::endl;

  // Check if file exists
  auto it = files.find(filename);
  if (it == files.end()) {
    std::cout << "[FILE_STORE] File not found for append: " << filename << std::endl;
    return false;  // File doesn't exist
  }

  // Add block
  blocks[block.block_id] = block;
  it->second.block_ids.push_back(block.block_id);
  it->second.total_size += block.size;

  auto now = std::chrono::system_clock::now();
  it->second.last_modified_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now.time_since_epoch())
                                           .count();
  it->second.version++;

  std::cout << "[FILE_STORE] Block appended successfully: " << filename << std::endl;
  return true;
}

std::vector<char> FileStore::getFile(const std::string& filename) const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return {};  // File doesn't exist
  }

  // Assemble file from blocks
  std::vector<char> file_data;
  file_data.reserve(it->second.total_size);

  for (uint64_t block_id : it->second.block_ids) {
    auto block_it = blocks.find(block_id);
    if (block_it != blocks.end()) {
      file_data.insert(file_data.end(), block_it->second.data.begin(),
                      block_it->second.data.end());
    }
  }

  return file_data;
}

std::vector<FileBlock> FileStore::getFileBlocks(const std::string& filename) const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return {};
  }

  std::vector<FileBlock> file_blocks;
  for (uint64_t block_id : it->second.block_ids) {
    auto block_it = blocks.find(block_id);
    if (block_it != blocks.end()) {
      file_blocks.push_back(block_it->second);
    }
  }

  return file_blocks;
}

FileMetadata FileStore::getFileMetadata(const std::string& filename) const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return {};
  }

  return it->second;
}

bool FileStore::hasFile(const std::string& filename) const {
  std::shared_lock<std::shared_mutex> lock(mtx);
  return files.find(filename) != files.end();
}

std::vector<std::string> FileStore::listFiles() const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  std::vector<std::string> file_list;
  for (const auto& entry : files) {
    file_list.push_back(entry.first);
  }

  return file_list;
}

bool FileStore::mergeFile(const std::string& filename, std::vector<FileBlock>& all_blocks) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return false;
  }

  // Clear existing blocks for this file
  for (uint64_t block_id : it->second.block_ids) {
    blocks.erase(block_id);
  }
  it->second.block_ids.clear();

  // Add merged blocks
  size_t total_size = 0;
  for (const auto& block : all_blocks) {
    blocks[block.block_id] = block;
    it->second.block_ids.push_back(block.block_id);
    total_size += block.size;
  }

  it->second.total_size = total_size;
  it->second.version++;

  auto now = std::chrono::system_clock::now();
  it->second.last_modified_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now.time_since_epoch())
                                           .count();

  return true;
}

bool FileStore::deleteFile(const std::string& filename) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return false;
  }

  // Delete all blocks from memory
  for (uint64_t block_id : it->second.block_ids) {
    blocks.erase(block_id);
  }

  // Delete metadata from memory
  files.erase(it);

  return true;
}

void FileStore::clearAllFiles() {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Clear all in-memory structures
  files.clear();
  blocks.clear();
}

bool FileStore::storeFile(const FileMetadata& metadata, const std::vector<FileBlock>& file_blocks) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Store metadata in memory
  files[metadata.hydfs_filename] = metadata;

  // Store blocks in memory
  for (const auto& block : file_blocks) {
    blocks[block.block_id] = block;
  }

  return true;
}

// Note: Persistence functions removed - in-memory only storage
void FileStore::persistMetadata(const std::string& /* filename */) {
  // No-op: In-memory only
}

void FileStore::persistBlock(uint64_t /* block_id */) {
  // No-op: In-memory only
}

void FileStore::loadMetadata(const std::string& /* filename */) {
  // No-op: In-memory only
}

void FileStore::loadBlock(uint64_t /* block_id */) {
  // No-op: In-memory only
}

std::string FileStore::getMetadataPath(const std::string& filename) const {
  return storage_dir + "/metadata/" + filename + ".meta";
}

std::string FileStore::getBlockPath(uint64_t block_id) const {
  return storage_dir + "/blocks/" + std::to_string(block_id) + ".blk";
}

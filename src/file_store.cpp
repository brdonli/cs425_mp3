#include "file_store.hpp"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>

FileStore::FileStore(const std::string& storage_dir) : storage_dir(storage_dir) {
  // Create storage directory if it doesn't exist
  std::filesystem::create_directories(storage_dir);
  std::filesystem::create_directories(storage_dir + "/metadata");
  std::filesystem::create_directories(storage_dir + "/blocks");

  // Load existing files from disk (for crash recovery)
  std::string metadata_dir = storage_dir + "/metadata";
  if (std::filesystem::exists(metadata_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(metadata_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".meta") {
        // Extract filename from path (remove .meta extension)
        std::string filename = entry.path().stem().string();
        loadMetadata(filename);

        // Load all blocks referenced by this metadata
        auto it = files.find(filename);
        if (it != files.end()) {
          for (uint64_t block_id : it->second.block_ids) {
            loadBlock(block_id);
          }
        }
      }
    }
    std::cout << "Loaded " << files.size() << " file(s) from disk at startup" << std::endl;
  }
}

bool FileStore::createFile(const std::string& filename, const std::vector<char>& data,
                           const std::string& client_id) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Check if file already exists
  if (files.find(filename) != files.end()) {
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
    persistBlock(block.block_id);
  }

  files[filename] = metadata;
  persistMetadata(filename);

  return true;
}

bool FileStore::appendBlock(const std::string& filename, const FileBlock& block) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Check if file exists
  auto it = files.find(filename);
  if (it == files.end()) {
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

  persistBlock(block.block_id);
  persistMetadata(filename);

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
    persistBlock(block.block_id);
  }

  it->second.total_size = total_size;
  it->second.version++;

  auto now = std::chrono::system_clock::now();
  it->second.last_modified_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now.time_since_epoch())
                                           .count();

  persistMetadata(filename);

  return true;
}

bool FileStore::deleteFile(const std::string& filename) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  auto it = files.find(filename);
  if (it == files.end()) {
    return false;
  }

  // Delete all blocks
  for (uint64_t block_id : it->second.block_ids) {
    blocks.erase(block_id);
    std::filesystem::remove(getBlockPath(block_id));
  }

  // Delete metadata
  std::filesystem::remove(getMetadataPath(filename));
  files.erase(it);

  return true;
}

void FileStore::clearAllFiles() {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Clear all in-memory structures
  files.clear();
  blocks.clear();

  // Delete all files on disk
  std::filesystem::remove_all(storage_dir + "/metadata");
  std::filesystem::remove_all(storage_dir + "/blocks");
  std::filesystem::create_directories(storage_dir + "/metadata");
  std::filesystem::create_directories(storage_dir + "/blocks");
}

bool FileStore::storeFile(const FileMetadata& metadata, const std::vector<FileBlock>& file_blocks) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Store metadata
  files[metadata.hydfs_filename] = metadata;
  persistMetadata(metadata.hydfs_filename);

  // Store blocks
  for (const auto& block : file_blocks) {
    blocks[block.block_id] = block;
    persistBlock(block.block_id);
  }

  return true;
}

void FileStore::persistMetadata(const std::string& filename) {
  auto it = files.find(filename);
  if (it == files.end()) {
    return;  // File doesn't exist in memory
  }

  std::string path = getMetadataPath(filename);
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to persist metadata for: " << filename << std::endl;
    return;
  }

  // Serialize metadata to buffer
  char buffer[65536];
  size_t size = it->second.serialize(buffer, sizeof(buffer));

  if (size > 0) {
    file.write(buffer, size);
  }
}

void FileStore::persistBlock(uint64_t block_id) {
  auto it = blocks.find(block_id);
  if (it == blocks.end()) {
    return;  // Block doesn't exist in memory
  }

  std::string path = getBlockPath(block_id);
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to persist block: " << block_id << std::endl;
    return;
  }

  // Serialize block to buffer
  char buffer[65536];
  size_t size = it->second.serialize(buffer, sizeof(buffer));

  if (size > 0) {
    file.write(buffer, size);
  }
}

void FileStore::loadMetadata(const std::string& filename) {
  std::string path = getMetadataPath(filename);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return;  // File doesn't exist on disk
  }

  // Read file contents
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    std::cerr << "Failed to read metadata file: " << filename << std::endl;
    return;
  }

  // Deserialize and store in memory
  FileMetadata metadata = FileMetadata::deserialize(buffer.data(), buffer.size());
  files[filename] = metadata;
}

void FileStore::loadBlock(uint64_t block_id) {
  std::string path = getBlockPath(block_id);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return;  // File doesn't exist on disk
  }

  // Read file contents
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    std::cerr << "Failed to read block file: " << block_id << std::endl;
    return;
  }

  // Deserialize and store in memory
  FileBlock block = FileBlock::deserialize(buffer.data(), buffer.size());
  blocks[block_id] = block;
}

std::string FileStore::getMetadataPath(const std::string& filename) const {
  return storage_dir + "/metadata/" + filename + ".meta";
}

std::string FileStore::getBlockPath(uint64_t block_id) const {
  return storage_dir + "/blocks/" + std::to_string(block_id) + ".blk";
}

#include "file_metadata.hpp"

#include <cstring>
#include <functional>

uint64_t FileMetadata::generateFileId(const std::string& filename) {
  return std::hash<std::string>{}(filename);
}

size_t FileMetadata::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  // Serialize filename length and data
  uint32_t filename_len = hydfs_filename.length();
  if (offset + sizeof(filename_len) + filename_len > buffer_size) return 0;
  std::memcpy(buffer + offset, &filename_len, sizeof(filename_len));
  offset += sizeof(filename_len);
  std::memcpy(buffer + offset, hydfs_filename.c_str(), filename_len);
  offset += filename_len;

  // Serialize file_id
  if (offset + sizeof(file_id) > buffer_size) return 0;
  std::memcpy(buffer + offset, &file_id, sizeof(file_id));
  offset += sizeof(file_id);

  // Serialize total_size
  if (offset + sizeof(total_size) > buffer_size) return 0;
  std::memcpy(buffer + offset, &total_size, sizeof(total_size));
  offset += sizeof(total_size);

  // Serialize version
  if (offset + sizeof(version) > buffer_size) return 0;
  std::memcpy(buffer + offset, &version, sizeof(version));
  offset += sizeof(version);

  // Serialize timestamps
  if (offset + sizeof(created_timestamp) > buffer_size) return 0;
  std::memcpy(buffer + offset, &created_timestamp, sizeof(created_timestamp));
  offset += sizeof(created_timestamp);

  if (offset + sizeof(last_modified_timestamp) > buffer_size) return 0;
  std::memcpy(buffer + offset, &last_modified_timestamp, sizeof(last_modified_timestamp));
  offset += sizeof(last_modified_timestamp);

  // Serialize block_ids count
  uint32_t block_count = block_ids.size();
  if (offset + sizeof(block_count) > buffer_size) return 0;
  std::memcpy(buffer + offset, &block_count, sizeof(block_count));
  offset += sizeof(block_count);

  // Serialize block_ids
  size_t blocks_size = block_count * sizeof(uint64_t);
  if (offset + blocks_size > buffer_size) return 0;
  std::memcpy(buffer + offset, block_ids.data(), blocks_size);
  offset += blocks_size;

  return offset;
}

FileMetadata FileMetadata::deserialize(const char* buffer, size_t buffer_size) {
  FileMetadata metadata;
  size_t offset = 0;

  // Deserialize filename
  uint32_t filename_len = 0;
  if (offset + sizeof(filename_len) > buffer_size) return metadata;
  std::memcpy(&filename_len, buffer + offset, sizeof(filename_len));
  offset += sizeof(filename_len);

  if (offset + filename_len > buffer_size) return metadata;
  metadata.hydfs_filename.resize(filename_len);
  std::memcpy(&metadata.hydfs_filename[0], buffer + offset, filename_len);
  offset += filename_len;

  // Deserialize file_id
  if (offset + sizeof(metadata.file_id) > buffer_size) return metadata;
  std::memcpy(&metadata.file_id, buffer + offset, sizeof(metadata.file_id));
  offset += sizeof(metadata.file_id);

  // Deserialize total_size
  if (offset + sizeof(metadata.total_size) > buffer_size) return metadata;
  std::memcpy(&metadata.total_size, buffer + offset, sizeof(metadata.total_size));
  offset += sizeof(metadata.total_size);

  // Deserialize version
  if (offset + sizeof(metadata.version) > buffer_size) return metadata;
  std::memcpy(&metadata.version, buffer + offset, sizeof(metadata.version));
  offset += sizeof(metadata.version);

  // Deserialize timestamps
  if (offset + sizeof(metadata.created_timestamp) > buffer_size) return metadata;
  std::memcpy(&metadata.created_timestamp, buffer + offset, sizeof(metadata.created_timestamp));
  offset += sizeof(metadata.created_timestamp);

  if (offset + sizeof(metadata.last_modified_timestamp) > buffer_size) return metadata;
  std::memcpy(&metadata.last_modified_timestamp, buffer + offset,
              sizeof(metadata.last_modified_timestamp));
  offset += sizeof(metadata.last_modified_timestamp);

  // Deserialize block_ids
  uint32_t block_count = 0;
  if (offset + sizeof(block_count) > buffer_size) return metadata;
  std::memcpy(&block_count, buffer + offset, sizeof(block_count));
  offset += sizeof(block_count);

  size_t blocks_size = block_count * sizeof(uint64_t);
  if (offset + blocks_size > buffer_size) return metadata;
  metadata.block_ids.resize(block_count);
  std::memcpy(metadata.block_ids.data(), buffer + offset, blocks_size);
  offset += blocks_size;

  return metadata;
}

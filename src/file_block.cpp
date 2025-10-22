#include "file_block.hpp"

#include <cstring>
#include <functional>

uint64_t FileBlock::generateBlockId(const std::string& client_id, uint64_t timestamp,
                                     uint32_t sequence_num) {
  // Combine client_id, timestamp, and sequence_num to create unique block ID
  std::string combined = client_id + std::to_string(timestamp) + std::to_string(sequence_num);
  return std::hash<std::string>{}(combined);
}

size_t FileBlock::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  // Serialize block_id
  if (offset + sizeof(block_id) > buffer_size) return 0;
  std::memcpy(buffer + offset, &block_id, sizeof(block_id));
  offset += sizeof(block_id);

  // Serialize client_id length and data
  uint32_t client_id_len = client_id.length();
  if (offset + sizeof(client_id_len) + client_id_len > buffer_size) return 0;
  std::memcpy(buffer + offset, &client_id_len, sizeof(client_id_len));
  offset += sizeof(client_id_len);
  std::memcpy(buffer + offset, client_id.c_str(), client_id_len);
  offset += client_id_len;

  // Serialize sequence_num
  if (offset + sizeof(sequence_num) > buffer_size) return 0;
  std::memcpy(buffer + offset, &sequence_num, sizeof(sequence_num));
  offset += sizeof(sequence_num);

  // Serialize timestamp
  if (offset + sizeof(timestamp) > buffer_size) return 0;
  std::memcpy(buffer + offset, &timestamp, sizeof(timestamp));
  offset += sizeof(timestamp);

  // Serialize size
  if (offset + sizeof(size) > buffer_size) return 0;
  std::memcpy(buffer + offset, &size, sizeof(size));
  offset += sizeof(size);

  // Serialize data
  if (offset + size > buffer_size) return 0;
  std::memcpy(buffer + offset, data.data(), size);
  offset += size;

  return offset;
}

FileBlock FileBlock::deserialize(const char* buffer, size_t buffer_size) {
  FileBlock block;
  size_t offset = 0;

  // Deserialize block_id
  if (offset + sizeof(block.block_id) > buffer_size) return block;
  std::memcpy(&block.block_id, buffer + offset, sizeof(block.block_id));
  offset += sizeof(block.block_id);

  // Deserialize client_id
  uint32_t client_id_len = 0;
  if (offset + sizeof(client_id_len) > buffer_size) return block;
  std::memcpy(&client_id_len, buffer + offset, sizeof(client_id_len));
  offset += sizeof(client_id_len);

  if (offset + client_id_len > buffer_size) return block;
  block.client_id.resize(client_id_len);
  std::memcpy(&block.client_id[0], buffer + offset, client_id_len);
  offset += client_id_len;

  // Deserialize sequence_num
  if (offset + sizeof(block.sequence_num) > buffer_size) return block;
  std::memcpy(&block.sequence_num, buffer + offset, sizeof(block.sequence_num));
  offset += sizeof(block.sequence_num);

  // Deserialize timestamp
  if (offset + sizeof(block.timestamp) > buffer_size) return block;
  std::memcpy(&block.timestamp, buffer + offset, sizeof(block.timestamp));
  offset += sizeof(block.timestamp);

  // Deserialize size
  if (offset + sizeof(block.size) > buffer_size) return block;
  std::memcpy(&block.size, buffer + offset, sizeof(block.size));
  offset += sizeof(block.size);

  // Deserialize data
  if (offset + block.size > buffer_size) return block;
  block.data.resize(block.size);
  std::memcpy(block.data.data(), buffer + offset, block.size);
  offset += block.size;

  return block;
}

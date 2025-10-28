#include "file_metadata.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

// macOS compatibility for byte order functions
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

// Helper function to serialize a string
static size_t serializeString(char* buffer, size_t buffer_size, size_t offset,
                               const std::string& str) {
  uint32_t str_len = str.length();
  if (offset + sizeof(str_len) + str_len > buffer_size) {
    throw std::runtime_error("Buffer too small for string");
  }

  uint32_t network_len = htonl(str_len);
  std::memcpy(buffer + offset, &network_len, sizeof(network_len));
  offset += sizeof(network_len);

  std::memcpy(buffer + offset, str.c_str(), str_len);
  offset += str_len;

  return offset;
}

// Helper function to deserialize a string
static std::string deserializeString(const char* buffer, size_t buffer_size, size_t& offset) {
  if (offset + sizeof(uint32_t) > buffer_size) {
    throw std::runtime_error("Buffer too small for string length");
  }

  uint32_t network_len;
  std::memcpy(&network_len, buffer + offset, sizeof(network_len));
  uint32_t str_len = ntohl(network_len);
  offset += sizeof(network_len);

  if (offset + str_len > buffer_size) {
    throw std::runtime_error("Buffer too small for string data");
  }

  std::string str(buffer + offset, str_len);
  offset += str_len;

  return str;
}

// Helper to serialize vector<char>
static size_t serializeData(char* buffer, size_t buffer_size, size_t offset,
                             const std::vector<char>& data) {
  uint64_t data_size = data.size();
  uint64_t network_size = htobe64(data_size);

  if (offset + sizeof(network_size) + data_size > buffer_size) {
    throw std::runtime_error("Buffer too small for data");
  }

  std::memcpy(buffer + offset, &network_size, sizeof(network_size));
  offset += sizeof(network_size);

  if (!data.empty()) {
    std::memcpy(buffer + offset, data.data(), data_size);
    offset += data_size;
  }

  return offset;
}

// Helper to deserialize vector<char>
static std::vector<char> deserializeData(const char* buffer, size_t buffer_size, size_t& offset) {
  if (offset + sizeof(uint64_t) > buffer_size) {
    throw std::runtime_error("Buffer too small for data size");
  }

  uint64_t network_size;
  std::memcpy(&network_size, buffer + offset, sizeof(network_size));
  uint64_t data_size = be64toh(network_size);
  offset += sizeof(network_size);

  if (offset + data_size > buffer_size) {
    throw std::runtime_error("Buffer too small for data");
  }

  std::vector<char> data(data_size);
  if (data_size > 0) {
    std::memcpy(data.data(), buffer + offset, data_size);
    offset += data_size;
  }

  return data;
}

// ===== CreateFileRequest =====
size_t CreateFileRequest::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);
  offset = serializeString(buffer, buffer_size, offset, local_filename);

  uint64_t network_client_id = htobe64(client_id);
  if (offset + sizeof(network_client_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_client_id, sizeof(network_client_id));
  offset += sizeof(network_client_id);

  offset = serializeData(buffer, buffer_size, offset, data);

  return offset;
}

CreateFileRequest CreateFileRequest::deserialize(const char* buffer, size_t buffer_size) {
  CreateFileRequest req;
  size_t offset = 0;

  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  req.local_filename = deserializeString(buffer, buffer_size, offset);

  uint64_t network_client_id;
  std::memcpy(&network_client_id, buffer + offset, sizeof(network_client_id));
  req.client_id = be64toh(network_client_id);
  offset += sizeof(network_client_id);

  req.data = deserializeData(buffer, buffer_size, offset);
  req.data_size = req.data.size();

  return req;
}

// ===== CreateFileResponse =====
size_t CreateFileResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  buffer[offset] = success ? 1 : 0;
  offset += 1;

  offset = serializeString(buffer, buffer_size, offset, error_message);

  uint64_t network_file_id = htobe64(file_id);
  if (offset + sizeof(network_file_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_file_id, sizeof(network_file_id));
  offset += sizeof(network_file_id);

  return offset;
}

CreateFileResponse CreateFileResponse::deserialize(const char* buffer, size_t buffer_size) {
  CreateFileResponse resp;
  size_t offset = 0;

  resp.success = buffer[offset] != 0;
  offset += 1;

  resp.error_message = deserializeString(buffer, buffer_size, offset);

  uint64_t network_file_id;
  std::memcpy(&network_file_id, buffer + offset, sizeof(network_file_id));
  resp.file_id = be64toh(network_file_id);
  offset += sizeof(network_file_id);

  return resp;
}

// ===== GetFileRequest =====
size_t GetFileRequest::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);
  offset = serializeString(buffer, buffer_size, offset, local_filename);

  uint64_t network_client_id = htobe64(client_id);
  if (offset + sizeof(network_client_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_client_id, sizeof(network_client_id));
  offset += sizeof(network_client_id);

  uint32_t network_seq = htonl(last_known_sequence);
  if (offset + sizeof(network_seq) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_seq, sizeof(network_seq));
  offset += sizeof(network_seq);

  return offset;
}

GetFileRequest GetFileRequest::deserialize(const char* buffer, size_t buffer_size) {
  GetFileRequest req;
  size_t offset = 0;

  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  req.local_filename = deserializeString(buffer, buffer_size, offset);

  uint64_t network_client_id;
  std::memcpy(&network_client_id, buffer + offset, sizeof(network_client_id));
  req.client_id = be64toh(network_client_id);
  offset += sizeof(network_client_id);

  uint32_t network_seq;
  std::memcpy(&network_seq, buffer + offset, sizeof(network_seq));
  req.last_known_sequence = ntohl(network_seq);
  offset += sizeof(network_seq);

  return req;
}

// ===== GetFileResponse =====
size_t GetFileResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  buffer[offset] = success ? 1 : 0;
  offset += 1;

  offset = serializeString(buffer, buffer_size, offset, error_message);

  // Serialize metadata
  size_t metadata_size = metadata.serialize(buffer + offset, buffer_size - offset);
  if (metadata_size == 0) {
    throw std::runtime_error("Failed to serialize metadata");
  }
  offset += metadata_size;

  // Serialize blocks count
  uint32_t block_count = blocks.size();
  uint32_t network_count = htonl(block_count);
  if (offset + sizeof(network_count) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_count, sizeof(network_count));
  offset += sizeof(network_count);

  // Serialize each block
  for (const auto& block : blocks) {
    size_t block_size = block.serialize(buffer + offset, buffer_size - offset);
    if (block_size == 0) {
      throw std::runtime_error("Failed to serialize block");
    }
    offset += block_size;
  }

  return offset;
}

GetFileResponse GetFileResponse::deserialize(const char* buffer, size_t buffer_size) {
  GetFileResponse resp;
  size_t offset = 0;

  if (buffer_size < 1) {
    return resp;  // Buffer too small
  }

  resp.success = buffer[offset] != 0;
  offset += 1;

  resp.error_message = deserializeString(buffer, buffer_size, offset);

  // For failed responses, metadata and blocks may be empty
  if (!resp.success) {
    return resp;
  }

  // Deserialize metadata - need to calculate actual size consumed
  resp.metadata = FileMetadata::deserialize(buffer + offset, buffer_size - offset);

  // Calculate actual metadata size by re-serializing (safest approach)
  // Metadata format: filename_len(4) + filename + file_id(8) + total_size(8) +
  //                  version(4) + created_ts(8) + modified_ts(8) + block_count(4) + block_ids
  offset += sizeof(uint32_t);  // filename length
  offset += resp.metadata.hydfs_filename.length();  // filename data
  offset += sizeof(uint64_t);  // file_id
  offset += sizeof(uint64_t);  // total_size
  offset += sizeof(uint32_t);  // version
  offset += sizeof(uint64_t);  // created_timestamp
  offset += sizeof(uint64_t);  // last_modified_timestamp
  offset += sizeof(uint32_t);  // block_count
  offset += resp.metadata.block_ids.size() * sizeof(uint64_t);  // block_ids array

  // Check if we have enough buffer left for block count
  if (offset + sizeof(uint32_t) > buffer_size) {
    return resp;  // Not enough data
  }

  // Deserialize blocks
  uint32_t network_count;
  std::memcpy(&network_count, buffer + offset, sizeof(network_count));
  uint32_t block_count = ntohl(network_count);
  offset += sizeof(network_count);

  for (uint32_t i = 0; i < block_count; ++i) {
    if (offset >= buffer_size) {
      break;  // Prevent buffer overrun
    }

    std::cout << "[DESER] Block " << i << " starts at offset " << offset << std::endl;

    FileBlock block = FileBlock::deserialize(buffer + offset, buffer_size - offset);
    resp.blocks.push_back(block);

    std::cout << "[DESER] Deserialized block: size=" << block.size
              << ", data.size()=" << block.data.size()
              << ", client_id.length()=" << block.client_id.length() << std::endl;

    // Calculate actual block size by reading directly from buffer
    // Block format: block_id(8) + client_id_len(4) + client_id + sequence_num(4) +
    //              timestamp(8) + size(4) + data
    size_t block_offset = 0;
    block_offset += sizeof(uint64_t);  // block_id

    // Read client_id length from buffer
    uint32_t client_id_len = 0;
    if (offset + block_offset + sizeof(client_id_len) <= buffer_size) {
      std::memcpy(&client_id_len, buffer + offset + block_offset, sizeof(client_id_len));
    }
    std::cout << "[DESER] Read client_id_len from buffer: " << client_id_len << std::endl;
    block_offset += sizeof(uint32_t);  // client_id length
    block_offset += client_id_len;  // client_id data

    block_offset += sizeof(uint32_t);  // sequence_num
    block_offset += sizeof(uint64_t);  // timestamp

    // Read actual data size from buffer
    uint32_t data_size = 0;
    if (offset + block_offset + sizeof(data_size) <= buffer_size) {
      std::memcpy(&data_size, buffer + offset + block_offset, sizeof(data_size));
    }
    std::cout << "[DESER] Read data_size from buffer: " << data_size << std::endl;
    block_offset += sizeof(uint32_t);  // size field
    block_offset += data_size;  // actual data

    std::cout << "[DESER] Block " << i << " consumed " << block_offset << " bytes, next offset: " << (offset + block_offset) << std::endl;
    offset += block_offset;
  }

  return resp;
}

// ===== AppendFileRequest =====
size_t AppendFileRequest::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);
  offset = serializeString(buffer, buffer_size, offset, local_filename);

  uint64_t network_client_id = htobe64(client_id);
  if (offset + sizeof(network_client_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_client_id, sizeof(network_client_id));
  offset += sizeof(network_client_id);

  uint32_t network_seq = htonl(sequence_num);
  if (offset + sizeof(network_seq) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_seq, sizeof(network_seq));
  offset += sizeof(network_seq);

  offset = serializeData(buffer, buffer_size, offset, data);

  return offset;
}

AppendFileRequest AppendFileRequest::deserialize(const char* buffer, size_t buffer_size) {
  AppendFileRequest req;
  size_t offset = 0;

  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  req.local_filename = deserializeString(buffer, buffer_size, offset);

  uint64_t network_client_id;
  std::memcpy(&network_client_id, buffer + offset, sizeof(network_client_id));
  req.client_id = be64toh(network_client_id);
  offset += sizeof(network_client_id);

  uint32_t network_seq;
  std::memcpy(&network_seq, buffer + offset, sizeof(network_seq));
  req.sequence_num = ntohl(network_seq);
  offset += sizeof(network_seq);

  req.data = deserializeData(buffer, buffer_size, offset);
  req.data_size = req.data.size();

  return req;
}

// ===== AppendFileResponse =====
size_t AppendFileResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  buffer[offset] = success ? 1 : 0;
  offset += 1;

  offset = serializeString(buffer, buffer_size, offset, error_message);

  uint64_t network_block_id = htobe64(block_id);
  if (offset + sizeof(network_block_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_block_id, sizeof(network_block_id));
  offset += sizeof(network_block_id);

  return offset;
}

AppendFileResponse AppendFileResponse::deserialize(const char* buffer, size_t buffer_size) {
  AppendFileResponse resp;
  size_t offset = 0;

  resp.success = buffer[offset] != 0;
  offset += 1;

  resp.error_message = deserializeString(buffer, buffer_size, offset);

  uint64_t network_block_id;
  std::memcpy(&network_block_id, buffer + offset, sizeof(network_block_id));
  resp.block_id = be64toh(network_block_id);
  offset += sizeof(network_block_id);

  return resp;
}

// ===== MergeFileRequest =====
size_t MergeFileRequest::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);

  buffer[offset] = is_coordinator ? 1 : 0;
  offset += 1;

  return offset;
}

MergeFileRequest MergeFileRequest::deserialize(const char* buffer, size_t buffer_size) {
  MergeFileRequest req;
  size_t offset = 0;

  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);

  req.is_coordinator = buffer[offset] != 0;
  offset += 1;

  return req;
}

// ===== MergeFileResponse =====
size_t MergeFileResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  buffer[offset] = success ? 1 : 0;
  offset += 1;

  offset = serializeString(buffer, buffer_size, offset, error_message);

  uint32_t network_version = htonl(new_version);
  if (offset + sizeof(network_version) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_version, sizeof(network_version));
  offset += sizeof(network_version);

  return offset;
}

MergeFileResponse MergeFileResponse::deserialize(const char* buffer, size_t buffer_size) {
  MergeFileResponse resp;
  size_t offset = 0;

  resp.success = buffer[offset] != 0;
  offset += 1;

  resp.error_message = deserializeString(buffer, buffer_size, offset);

  uint32_t network_version;
  std::memcpy(&network_version, buffer + offset, sizeof(network_version));
  resp.new_version = ntohl(network_version);
  offset += sizeof(network_version);

  return resp;
}

// ===== LsFileRequest =====
size_t LsFileRequest::serialize(char* buffer, size_t buffer_size) const {
  return serializeString(buffer, buffer_size, 0, hydfs_filename);
}

LsFileRequest LsFileRequest::deserialize(const char* buffer, size_t buffer_size) {
  LsFileRequest req;
  size_t offset = 0;
  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  return req;
}

// ===== LsFileResponse =====
size_t LsFileResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  buffer[offset] = success ? 1 : 0;
  offset += 1;

  offset = serializeString(buffer, buffer_size, offset, error_message);

  uint64_t network_file_id = htobe64(file_id);
  if (offset + sizeof(network_file_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_file_id, sizeof(network_file_id));
  offset += sizeof(network_file_id);

  // Serialize VM addresses
  uint32_t vm_count = vm_addresses.size();
  uint32_t network_count = htonl(vm_count);
  if (offset + sizeof(network_count) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_count, sizeof(network_count));
  offset += sizeof(network_count);

  for (const auto& vm_addr : vm_addresses) {
    offset = serializeString(buffer, buffer_size, offset, vm_addr);
  }

  // Serialize ring IDs
  if (offset + ring_ids.size() * sizeof(uint64_t) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }

  for (uint64_t ring_id : ring_ids) {
    uint64_t network_ring_id = htobe64(ring_id);
    std::memcpy(buffer + offset, &network_ring_id, sizeof(network_ring_id));
    offset += sizeof(network_ring_id);
  }

  return offset;
}

LsFileResponse LsFileResponse::deserialize(const char* buffer, size_t buffer_size) {
  LsFileResponse resp;
  size_t offset = 0;

  resp.success = buffer[offset] != 0;
  offset += 1;

  resp.error_message = deserializeString(buffer, buffer_size, offset);

  uint64_t network_file_id;
  std::memcpy(&network_file_id, buffer + offset, sizeof(network_file_id));
  resp.file_id = be64toh(network_file_id);
  offset += sizeof(network_file_id);

  uint32_t network_count;
  std::memcpy(&network_count, buffer + offset, sizeof(network_count));
  uint32_t vm_count = ntohl(network_count);
  offset += sizeof(network_count);

  for (uint32_t i = 0; i < vm_count; ++i) {
    resp.vm_addresses.push_back(deserializeString(buffer, buffer_size, offset));
  }

  for (uint32_t i = 0; i < vm_count; ++i) {
    uint64_t network_ring_id;
    std::memcpy(&network_ring_id, buffer + offset, sizeof(network_ring_id));
    resp.ring_ids.push_back(be64toh(network_ring_id));
    offset += sizeof(network_ring_id);
  }

  return resp;
}

// ===== ListStoreRequest =====
size_t ListStoreRequest::serialize(char* /* buffer */, size_t /* buffer_size */) const {
  // Empty message
  return 0;
}

ListStoreRequest ListStoreRequest::deserialize(const char* /* buffer */, size_t /* buffer_size */) {
  return ListStoreRequest();
}

// ===== ListStoreResponse =====
size_t ListStoreResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  uint32_t file_count = filenames.size();
  uint32_t network_count = htonl(file_count);
  if (offset + sizeof(network_count) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_count, sizeof(network_count));
  offset += sizeof(network_count);

  for (const auto& filename : filenames) {
    offset = serializeString(buffer, buffer_size, offset, filename);
  }

  for (uint64_t file_id : file_ids) {
    uint64_t network_file_id = htobe64(file_id);
    if (offset + sizeof(network_file_id) > buffer_size) {
      throw std::runtime_error("Buffer too small");
    }
    std::memcpy(buffer + offset, &network_file_id, sizeof(network_file_id));
    offset += sizeof(network_file_id);
  }

  return offset;
}

ListStoreResponse ListStoreResponse::deserialize(const char* buffer, size_t buffer_size) {
  ListStoreResponse resp;
  size_t offset = 0;

  uint32_t network_count;
  std::memcpy(&network_count, buffer + offset, sizeof(network_count));
  uint32_t file_count = ntohl(network_count);
  offset += sizeof(network_count);

  for (uint32_t i = 0; i < file_count; ++i) {
    resp.filenames.push_back(deserializeString(buffer, buffer_size, offset));
  }

  for (uint32_t i = 0; i < file_count; ++i) {
    uint64_t network_file_id;
    std::memcpy(&network_file_id, buffer + offset, sizeof(network_file_id));
    resp.file_ids.push_back(be64toh(network_file_id));
    offset += sizeof(network_file_id);
  }

  return resp;
}

// ===== FileExistsRequest =====
size_t FileExistsRequest::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;
  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);
  offset = serializeString(buffer, buffer_size, offset, requester_id);
  return offset;
}

FileExistsRequest FileExistsRequest::deserialize(const char* buffer, size_t buffer_size) {
  FileExistsRequest req;
  size_t offset = 0;
  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  req.requester_id = deserializeString(buffer, buffer_size, offset);
  return req;
}

// ===== FileExistsResponse =====
size_t FileExistsResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);

  uint8_t exists_byte = exists ? 1 : 0;
  if (offset + sizeof(exists_byte) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &exists_byte, sizeof(exists_byte));
  offset += sizeof(exists_byte);

  uint64_t network_file_id = htobe64(file_id);
  if (offset + sizeof(network_file_id) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_file_id, sizeof(network_file_id));
  offset += sizeof(network_file_id);

  uint64_t network_file_size = htobe64(file_size);
  if (offset + sizeof(network_file_size) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_file_size, sizeof(network_file_size));
  offset += sizeof(network_file_size);

  uint32_t network_version = htonl(version);
  if (offset + sizeof(network_version) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_version, sizeof(network_version));
  offset += sizeof(network_version);

  return offset;
}

FileExistsResponse FileExistsResponse::deserialize(const char* buffer, size_t buffer_size) {
  FileExistsResponse resp;
  size_t offset = 0;

  resp.hydfs_filename = deserializeString(buffer, buffer_size, offset);

  uint8_t exists_byte;
  std::memcpy(&exists_byte, buffer + offset, sizeof(exists_byte));
  resp.exists = (exists_byte != 0);
  offset += sizeof(exists_byte);

  uint64_t network_file_id;
  std::memcpy(&network_file_id, buffer + offset, sizeof(network_file_id));
  resp.file_id = be64toh(network_file_id);
  offset += sizeof(network_file_id);

  uint64_t network_file_size;
  std::memcpy(&network_file_size, buffer + offset, sizeof(network_file_size));
  resp.file_size = be64toh(network_file_size);
  offset += sizeof(network_file_size);

  uint32_t network_version;
  std::memcpy(&network_version, buffer + offset, sizeof(network_version));
  resp.version = ntohl(network_version);
  offset += sizeof(network_version);

  return resp;
}

// ===== ReplicateBlockMessage =====
size_t ReplicateBlockMessage::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);

  size_t block_size = block.serialize(buffer + offset, buffer_size - offset);
  if (block_size == 0) {
    throw std::runtime_error("Failed to serialize block");
  }
  offset += block_size;

  return offset;
}

ReplicateBlockMessage ReplicateBlockMessage::deserialize(const char* buffer, size_t buffer_size) {
  ReplicateBlockMessage msg;
  size_t offset = 0;

  msg.hydfs_filename = deserializeString(buffer, buffer_size, offset);

  msg.block = FileBlock::deserialize(buffer + offset, buffer_size - offset);

  return msg;
}

// ===== CollectBlocksRequest =====
size_t CollectBlocksRequest::serialize(char* buffer, size_t buffer_size) const {
  return serializeString(buffer, buffer_size, 0, hydfs_filename);
}

CollectBlocksRequest CollectBlocksRequest::deserialize(const char* buffer, size_t buffer_size) {
  CollectBlocksRequest req;
  size_t offset = 0;
  req.hydfs_filename = deserializeString(buffer, buffer_size, offset);
  return req;
}

// ===== CollectBlocksResponse =====
size_t CollectBlocksResponse::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);

  uint32_t block_count = blocks.size();
  uint32_t network_count = htonl(block_count);
  if (offset + sizeof(network_count) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_count, sizeof(network_count));
  offset += sizeof(network_count);

  for (const auto& block : blocks) {
    size_t block_size = block.serialize(buffer + offset, buffer_size - offset);
    if (block_size == 0) {
      throw std::runtime_error("Failed to serialize block");
    }
    offset += block_size;
  }

  uint32_t network_version = htonl(version);
  if (offset + sizeof(network_version) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_version, sizeof(network_version));
  offset += sizeof(network_version);

  return offset;
}

CollectBlocksResponse CollectBlocksResponse::deserialize(const char* buffer, size_t buffer_size) {
  CollectBlocksResponse resp;
  size_t offset = 0;

  resp.hydfs_filename = deserializeString(buffer, buffer_size, offset);

  uint32_t network_count;
  std::memcpy(&network_count, buffer + offset, sizeof(network_count));
  uint32_t block_count = ntohl(network_count);
  offset += sizeof(network_count);

  for (uint32_t i = 0; i < block_count; ++i) {
    FileBlock block = FileBlock::deserialize(buffer + offset, buffer_size - offset);
    resp.blocks.push_back(block);
    // Calculate block offset
    offset += sizeof(uint64_t) * 3 + sizeof(uint32_t) * 2 + sizeof(uint32_t) +
              block.client_id.length() + block.size;
  }

  uint32_t network_version;
  std::memcpy(&network_version, buffer + offset, sizeof(network_version));
  resp.version = ntohl(network_version);
  offset += sizeof(network_version);

  return resp;
}

// ===== MergeUpdateMessage =====
size_t MergeUpdateMessage::serialize(char* buffer, size_t buffer_size) const {
  size_t offset = 0;

  offset = serializeString(buffer, buffer_size, offset, hydfs_filename);

  uint32_t block_count = merged_block_ids.size();
  uint32_t network_count = htonl(block_count);
  if (offset + sizeof(network_count) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_count, sizeof(network_count));
  offset += sizeof(network_count);

  for (uint64_t block_id : merged_block_ids) {
    uint64_t network_block_id = htobe64(block_id);
    if (offset + sizeof(network_block_id) > buffer_size) {
      throw std::runtime_error("Buffer too small");
    }
    std::memcpy(buffer + offset, &network_block_id, sizeof(network_block_id));
    offset += sizeof(network_block_id);
  }

  uint32_t network_version = htonl(new_version);
  if (offset + sizeof(network_version) > buffer_size) {
    throw std::runtime_error("Buffer too small");
  }
  std::memcpy(buffer + offset, &network_version, sizeof(network_version));
  offset += sizeof(network_version);

  return offset;
}

MergeUpdateMessage MergeUpdateMessage::deserialize(const char* buffer, size_t buffer_size) {
  MergeUpdateMessage msg;
  size_t offset = 0;

  msg.hydfs_filename = deserializeString(buffer, buffer_size, offset);

  uint32_t network_count;
  std::memcpy(&network_count, buffer + offset, sizeof(network_count));
  uint32_t block_count = ntohl(network_count);
  offset += sizeof(network_count);

  for (uint32_t i = 0; i < block_count; ++i) {
    uint64_t network_block_id;
    std::memcpy(&network_block_id, buffer + offset, sizeof(network_block_id));
    msg.merged_block_ids.push_back(be64toh(network_block_id));
    offset += sizeof(network_block_id);
  }

  uint32_t network_version;
  std::memcpy(&network_version, buffer + offset, sizeof(network_version));
  msg.new_version = ntohl(network_version);
  offset += sizeof(network_version);

  return msg;
}

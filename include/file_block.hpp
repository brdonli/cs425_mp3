#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Represents a single block of data in HyDFS
 * Each append operation creates a new block
 */
struct FileBlock {
  uint64_t block_id;        // unique ID (hash of client_id + timestamp + sequence)
  std::string client_id;    // who appended this (NodeId as string)
  uint32_t sequence_num;    // order within this client's appends
  uint64_t timestamp;       // when the append occurred
  std::vector<char> data;   // actual block data
  size_t size;              // size of data

  // Serialize block to buffer for network transmission
  size_t serialize(char* buffer, size_t buffer_size) const;

  // Deserialize block from buffer
  static FileBlock deserialize(const char* buffer, size_t buffer_size);

  // Generate unique block ID
  static uint64_t generateBlockId(const std::string& client_id, uint64_t timestamp,
                                   uint32_t sequence_num);
};

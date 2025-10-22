#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Tracks client operations for read-my-writes consistency
 * Maintains which blocks each client has successfully appended
 */
class ClientTracker {
 public:
  ClientTracker() = default;

  // Record a successful append by a client
  void recordAppend(const std::string& client_id, const std::string& filename,
                    uint64_t block_id);

  // Get all block IDs that a client has appended to a file
  std::vector<uint64_t> getClientAppends(const std::string& client_id,
                                         const std::string& filename) const;

  // Check if a file version satisfies read-my-writes for a client
  bool satisfiesReadMyWrites(const std::string& client_id, const std::string& filename,
                            const std::vector<uint64_t>& file_block_ids) const;

  // Clear all tracking for a client
  void clearClient(const std::string& client_id);

  // Clear all tracking for a file
  void clearFile(const std::string& filename);

 private:
  // client_id -> (filename -> list of block_ids)
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::vector<uint64_t>>>
      client_appends;

  mutable std::shared_mutex mtx;  // thread safety
};

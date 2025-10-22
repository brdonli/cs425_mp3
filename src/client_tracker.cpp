#include "client_tracker.hpp"

#include <algorithm>
#include <mutex>

void ClientTracker::recordAppend(const std::string& client_id, const std::string& filename,
                                 uint64_t block_id) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  client_appends[client_id][filename].push_back(block_id);
}

std::vector<uint64_t> ClientTracker::getClientAppends(const std::string& client_id,
                                                      const std::string& filename) const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  auto client_it = client_appends.find(client_id);
  if (client_it == client_appends.end()) {
    return {};
  }

  auto file_it = client_it->second.find(filename);
  if (file_it == client_it->second.end()) {
    return {};
  }

  return file_it->second;
}

bool ClientTracker::satisfiesReadMyWrites(const std::string& client_id,
                                         const std::string& filename,
                                         const std::vector<uint64_t>& file_block_ids) const {
  std::shared_lock<std::shared_mutex> lock(mtx);

  auto client_it = client_appends.find(client_id);
  if (client_it == client_appends.end()) {
    return true;  // Client has no appends, so any version is fine
  }

  auto file_it = client_it->second.find(filename);
  if (file_it == client_it->second.end()) {
    return true;  // Client has no appends to this file
  }

  // Check that all of the client's appended blocks are in the file
  for (uint64_t block_id : file_it->second) {
    if (std::find(file_block_ids.begin(), file_block_ids.end(), block_id) ==
        file_block_ids.end()) {
      return false;  // Missing a block that the client appended
    }
  }

  return true;
}

void ClientTracker::clearClient(const std::string& client_id) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  client_appends.erase(client_id);
}

void ClientTracker::clearFile(const std::string& filename) {
  std::unique_lock<std::shared_mutex> lock(mtx);

  // Remove this filename from all clients
  for (auto& client_entry : client_appends) {
    client_entry.second.erase(filename);
  }
}

#include "file_operations_handler.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

FileOperationsHandler::FileOperationsHandler(FileStore& file_store,
                                             ConsistentHashRing& hash_ring,
                                             const NodeId& self_id, Logger& logger,
                                             UDPSocketConnection& socket)
    : file_store_(file_store),
      hash_ring_(hash_ring),
      self_id_(self_id),
      logger_(logger),
      socket_(socket) {
  // Load all files from test_files/ directory into local cache
  loadTestFiles();
}

void FileOperationsHandler::loadTestFiles() {
  std::cout << "[LOCAL_CACHE] Loading files from test_files/ directory..." << std::endl;

  // Use system command to list files in test_files/
  FILE* pipe = popen("ls test_files/ 2>/dev/null", "r");
  if (!pipe) {
    std::cout << "[LOCAL_CACHE] No test_files/ directory found, starting with empty cache" << std::endl;
    return;
  }

  char filename[256];
  int files_loaded = 0;

  while (fgets(filename, sizeof(filename), pipe) != nullptr) {
    // Remove newline
    filename[strcspn(filename, "\n")] = 0;

    std::string filepath = std::string("test_files/") + filename;

    // Read file into memory
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      continue;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size)) {
      std::lock_guard<std::mutex> lock(local_cache_mtx_);
      local_file_cache_[filename] = buffer;
      files_loaded++;
      std::cout << "[LOCAL_CACHE]   Loaded: " << filename << " (" << size << " bytes)" << std::endl;
    }
  }

  pclose(pipe);
  std::cout << "[LOCAL_CACHE] Loaded " << files_loaded << " files into local cache" << std::endl;
}

bool FileOperationsHandler::getLocalFile(const std::string& filename, std::vector<char>& data) {
  std::lock_guard<std::mutex> lock(local_cache_mtx_);
  auto it = local_file_cache_.find(filename);
  if (it == local_file_cache_.end()) {
    return false;
  }
  data = it->second;
  return true;
}

void FileOperationsHandler::storeLocalFile(const std::string& filename, const std::vector<char>& data) {
  std::lock_guard<std::mutex> lock(local_cache_mtx_);
  local_file_cache_[filename] = data;
  std::cout << "[LOCAL_CACHE] Stored file in local cache: " << filename << " (" << data.size() << " bytes)" << std::endl;
}

std::string FileOperationsHandler::getClientId() const {
  std::stringstream ss;
  ss << self_id_;
  return ss.str();
}

uint32_t FileOperationsHandler::getNextSequenceNum(const std::string& hydfs_filename) {
  std::lock_guard<std::mutex> lock(seq_mtx_);
  return sequence_numbers_[hydfs_filename]++;
}

bool FileOperationsHandler::isCoordinator(const std::string& hydfs_filename) const {
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);
  if (replicas.empty()) {
    return false;
  }
  return replicas[0] == self_id_;
}

bool FileOperationsHandler::sendFileMessage(FileMessageType type, const char* buffer,
                                            size_t buffer_size,
                                            const struct sockaddr_in& dest) {
  // Create a message buffer with type prefix using string for dynamic allocation
  std::string msg_buffer;
  msg_buffer.reserve(buffer_size + 1);
  msg_buffer.push_back(static_cast<char>(type));
  msg_buffer.append(buffer, std::min(buffer_size, static_cast<size_t>(UDPSocketConnection::BUFFER_LEN - 1)));

  char dest_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(dest.sin_addr), dest_ip, INET_ADDRSTRLEN);
  std::cout << "[SEND_FILE_MSG] Type: " << static_cast<int>(type)
            << " to " << dest_ip << ":" << ntohs(dest.sin_port)
            << " size: " << msg_buffer.size() << " bytes" << std::endl;

  ssize_t sent = socket_.write_to_socket(msg_buffer, dest);

  std::cout << "[SEND_FILE_MSG] Sent " << sent << " bytes (expected " << msg_buffer.size() << ")" << std::endl;

  return sent > 0;
}

bool FileOperationsHandler::replicateBlock(const std::string& hydfs_filename,
                                           const FileBlock& block,
                                           const std::vector<NodeId>& replicas) {
  ReplicateBlockMessage msg;
  msg.hydfs_filename = hydfs_filename;
  msg.block = block;

  char buffer[8192];
  size_t size = msg.serialize(buffer, sizeof(buffer));

  bool all_success = true;
  for (const auto& replica : replicas) {
    if (replica == self_id_) {
      continue;  // Don't replicate to self
    }

    struct sockaddr_in dest_addr;
    socket_.buildServerAddr(dest_addr, replica.host, replica.port);

    if (!sendFileMessage(FileMessageType::REPLICATE_BLOCK, buffer, size, dest_addr)) {
      logger_.log("Failed to replicate block to " + std::string(replica.host) + ":" +
                  std::string(replica.port));
      all_success = false;
    }
  }

  return all_success;
}

// ===== Core File Operations =====

bool FileOperationsHandler::createFile(const std::string& local_filename,
                                       const std::string& hydfs_filename) {
  // Read from local cache instead of filesystem
  std::vector<char> data;
  if (!getLocalFile(local_filename, data)) {
    std::cout << "❌ Failed to find local file in cache: " << local_filename << std::endl;
    std::cout << "Hint: Use 'liststore' to see available local files" << std::endl;
    return false;
  }

  std::cout << "\n=== CREATE FILE OPERATION ===" << std::endl;
  std::cout << "Local file (from cache): " << local_filename << " (" << data.size() << " bytes)" << std::endl;
  std::cout << "HyDFS filename: " << hydfs_filename << std::endl;

  // Get replicas for this file (the n=3 successors in the ring)
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);

  std::cout << "\n=== HASH RING STATUS ===" << std::endl;
  std::cout << "Total nodes in ring: " << hash_ring_.size() << std::endl;
  std::cout << "Replicas for '" << hydfs_filename << "': " << replicas.size() << std::endl;

  if (replicas.empty()) {
    std::cout << "❌ ERROR: No replicas available in the ring!" << std::endl;
    std::cout << "Make sure other VMs have joined the network." << std::endl;
    return false;
  }
  std::cout << "========================\n" << std::endl;

  // Create the initial file block
  FileBlock initial_block;
  initial_block.client_id = getClientId();
  initial_block.sequence_num = 0;
  auto now = std::chrono::system_clock::now();
  initial_block.timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  initial_block.data = data;
  initial_block.size = data.size();
  initial_block.block_id =
      FileBlock::generateBlockId(initial_block.client_id, initial_block.timestamp, 0);

  // Check if we are one of the replicas
  bool we_are_replica = false;
  for (const auto& replica : replicas) {
    if (replica == self_id_) {
      we_are_replica = true;
      break;
    }
  }

  if (we_are_replica) {
    // Store locally first
    bool success = file_store_.createFile(hydfs_filename, data, getClientId());
    if (!success) {
      std::cout << "File already exists in HyDFS\n";
      return false;
    }
    logger_.log("Created file locally: " + hydfs_filename);
  }

  // Send CREATE_REQUEST to all replicas (including ourselves if we're not one)
  // This ensures ANY VM can initiate a create operation
  std::cout << "\n=== SENDING CREATE REQUESTS ===" << std::endl;
  std::cout << "File: " << hydfs_filename << std::endl;
  std::cout << "Replicas determined by hash ring:" << std::endl;
  for (const auto& replica : replicas) {
    std::cout << "  - " << replica.host << ":" << replica.port << std::endl;
  }

  CreateFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.local_filename = local_filename;
  req.client_id = hash_ring_.getNodePosition(self_id_);  // Use uint64_t position
  req.data = data;
  req.data_size = data.size();

  char buffer[8192];
  size_t size = req.serialize(buffer, sizeof(buffer));

  std::cout << "Serialized message size: " << size << " bytes" << std::endl;

  int sent_count = 0;
  for (const auto& replica : replicas) {
    // Skip self if we already stored locally
    if (replica == self_id_ && we_are_replica) {
      std::cout << "  [SKIP] " << replica.host << ":" << replica.port
                << " (already stored locally)" << std::endl;
      sent_count++;
      continue;
    }

    struct sockaddr_in dest_addr;
    socket_.buildServerAddr(dest_addr, replica.host, replica.port);

    std::cout << "  [SEND] Sending to " << replica.host << ":" << replica.port << std::endl;
    std::cout << "         Port (network byte order): " << ntohs(dest_addr.sin_port) << std::endl;

    if (sendFileMessage(FileMessageType::CREATE_REQUEST, buffer, size, dest_addr)) {
      std::cout << "         Result: ✅ SUCCESS" << std::endl;
      sent_count++;
    } else {
      std::cout << "         Result: ❌ FAILED" << std::endl;
      logger_.log("Failed to send create request to " + std::string(replica.host) + ":" +
                  std::string(replica.port));
    }
  }
  std::cout << "================================\n" << std::endl;

  // Small delay to allow create requests to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "File created successfully and sent to " << sent_count << " replica(s): "
            << hydfs_filename << "\n";
  return true;
}

bool FileOperationsHandler::getFile(const std::string& hydfs_filename,
                                    const std::string& local_filename) {
  std::cout << "\n=== GET FILE OPERATION ===" << std::endl;
  std::cout << "HyDFS file: " << hydfs_filename << std::endl;
  std::cout << "Local file: " << local_filename << std::endl;

  // Check if we have it locally first
  if (file_store_.hasFile(hydfs_filename)) {
    std::cout << "File found locally, retrieving..." << std::endl;
    logger_.log("GET operation started for " + hydfs_filename + " (local)");

    std::vector<char> data = file_store_.getFile(hydfs_filename);

    // Check read-my-writes consistency
    std::string client_id = getClientId();
    std::vector<uint64_t> block_ids = file_store_.getFileMetadata(hydfs_filename).block_ids;

    if (!client_tracker_.satisfiesReadMyWrites(client_id, hydfs_filename, block_ids)) {
      std::cout << "❌ Local copy does not satisfy read-my-writes consistency" << std::endl;
      std::cout << "Fetching from remote replica instead..." << std::endl;
      // Fall through to remote fetch
    } else {
      // Store in local cache
      storeLocalFile(local_filename, data);
      std::cout << "✅ File retrieved successfully: " << hydfs_filename << " -> " << local_filename << std::endl;
      std::cout << "File size: " << data.size() << " bytes" << std::endl;
      logger_.log("GET operation completed for " + hydfs_filename + " (local)");
      std::cout << "========================\n" << std::endl;
      return true;
    }
  }

  // Find replicas for this file
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);
  if (replicas.empty()) {
    std::cout << "❌ No replicas found for file: " << hydfs_filename << std::endl;
    std::cout << "========================\n" << std::endl;
    return false;
  }

  std::cout << "Fetching from remote replica..." << std::endl;
  std::cout << "Available replicas: " << replicas.size() << std::endl;
  for (const auto& replica : replicas) {
    std::cout << "  - " << replica.host << ":" << replica.port << std::endl;
  }

  // Register pending get request
  {
    std::lock_guard<std::mutex> lock(pending_gets_mtx_);
    pending_gets_[hydfs_filename] = local_filename;
    get_results_.erase(hydfs_filename);  // Clear any old result
  }

  // Send GET request to first available replica (try others if first fails)
  bool request_sent = false;
  for (const auto& replica : replicas) {
    // Skip self if we already checked locally
    if (replica == self_id_) {
      continue;
    }

    GetFileRequest req;
    req.hydfs_filename = hydfs_filename;
    req.local_filename = local_filename;
    req.client_id = hash_ring_.getNodePosition(self_id_);
    req.last_known_sequence = 0;

    char buffer[8192];
    size_t size = req.serialize(buffer, sizeof(buffer));

    struct sockaddr_in dest_addr;
    socket_.buildServerAddr(dest_addr, replica.host, replica.port);

    std::cout << "Sending GET_REQUEST to " << replica.host << ":" << replica.port << std::endl;
    logger_.log("Sending GET_REQUEST for " + hydfs_filename + " to " +
                std::string(replica.host) + ":" + std::string(replica.port));

    if (sendFileMessage(FileMessageType::GET_REQUEST, buffer, size, dest_addr)) {
      request_sent = true;
      break;  // Successfully sent to one replica
    }
  }

  if (!request_sent) {
    std::cout << "❌ Failed to send get request to any replica" << std::endl;
    std::lock_guard<std::mutex> lock(pending_gets_mtx_);
    pending_gets_.erase(hydfs_filename);
    std::cout << "========================\n" << std::endl;
    return false;
  }

  // Wait for response with timeout
  std::unique_lock<std::mutex> lock(pending_gets_mtx_);
  bool received = get_cv_.wait_for(lock, std::chrono::seconds(5), [this, &hydfs_filename] {
    return get_results_.find(hydfs_filename) != get_results_.end();
  });

  if (!received) {
    std::cout << "❌ Timeout waiting for GET_RESPONSE" << std::endl;
    pending_gets_.erase(hydfs_filename);
    std::cout << "========================\n" << std::endl;
    return false;
  }

  bool success = get_results_[hydfs_filename];
  get_results_.erase(hydfs_filename);
  pending_gets_.erase(hydfs_filename);

  if (success) {
    std::cout << "✅ GET operation completed successfully" << std::endl;
    logger_.log("GET operation completed for " + hydfs_filename);
  } else {
    std::cout << "❌ GET operation failed" << std::endl;
    logger_.log("GET operation failed for " + hydfs_filename);
  }

  std::cout << "========================\n" << std::endl;
  return success;
}

bool FileOperationsHandler::appendFile(const std::string& local_filename,
                                       const std::string& hydfs_filename) {
  std::cout << "\n=== APPEND FILE OPERATION ===" << std::endl;
  std::cout << "Local file (from cache): " << local_filename << std::endl;
  std::cout << "HyDFS file: " << hydfs_filename << std::endl;

  // Read from local cache
  std::vector<char> data;
  if (!getLocalFile(local_filename, data)) {
    std::cout << "❌ Failed to find local file in cache: " << local_filename << std::endl;
    std::cout << "Hint: Use 'liststore' to see available local files" << std::endl;
    std::cout << "============================\n" << std::endl;
    return false;
  }

  std::cout << "Data to append: " << data.size() << " bytes" << std::endl;

  // Create append request
  AppendFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.local_filename = local_filename;
  req.client_id = hash_ring_.getNodePosition(self_id_);
  req.sequence_num = getNextSequenceNum(hydfs_filename);
  req.data = data;
  req.data_size = data.size();

  std::cout << "Sequence number: " << req.sequence_num << std::endl;

  char buffer[8192];
  size_t size = req.serialize(buffer, sizeof(buffer));

  if (size > sizeof(buffer)) {
    std::cout << "❌ Error: Data too large to send in single message" << std::endl;
    std::cout << "Maximum size: " << sizeof(buffer) << " bytes" << std::endl;
    std::cout << "============================\n" << std::endl;
    return false;
  }

  // Send to coordinator (first replica)
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);
  if (replicas.empty()) {
    std::cout << "❌ No replicas found for file" << std::endl;
    std::cout << "============================\n" << std::endl;
    return false;
  }

  NodeId coordinator = replicas[0];
  std::cout << "Coordinator: " << coordinator.host << ":" << coordinator.port << std::endl;

  struct sockaddr_in dest_addr;
  socket_.buildServerAddr(dest_addr, coordinator.host, coordinator.port);

  logger_.log("Sending APPEND_REQUEST for " + hydfs_filename + " to coordinator " +
              std::string(coordinator.host) + ":" + std::string(coordinator.port));

  if (!sendFileMessage(FileMessageType::APPEND_REQUEST, buffer, size, dest_addr)) {
    std::cout << "❌ Failed to send append request" << std::endl;
    std::cout << "============================\n" << std::endl;
    return false;
  }

  std::cout << "✅ Append request sent to coordinator" << std::endl;
  std::cout << "Data will be appended and replicated to all " << replicas.size() << " replicas" << std::endl;
  logger_.log("APPEND operation initiated for " + hydfs_filename);
  std::cout << "============================\n" << std::endl;

  return true;
}

bool FileOperationsHandler::mergeFile(const std::string& hydfs_filename) {
  MergeFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.is_coordinator = isCoordinator(hydfs_filename);

  char buffer[8192];
  size_t size = req.serialize(buffer, sizeof(buffer));

  // Send to coordinator
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);
  if (replicas.empty()) {
    std::cout << "No replicas found\n";
    return false;
  }

  struct sockaddr_in dest_addr;
  std::memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(std::atoi(replicas[0].port));
  inet_pton(AF_INET, replicas[0].host, &dest_addr.sin_addr);

  sendFileMessage(FileMessageType::MERGE_REQUEST, buffer, size, dest_addr);
  std::cout << "Merge request sent\n";
  return true;
}

void FileOperationsHandler::listFileLocations(const std::string& hydfs_filename) {
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);

  std::cout << "\n=== LS: Checking file existence across replicas ===" << std::endl;
  std::cout << "File: " << hydfs_filename << std::endl;
  std::cout << "File ID: " << FileMetadata::generateFileId(hydfs_filename) << std::endl;

  // Register pending ls request
  {
    std::lock_guard<std::mutex> lock(pending_ls_mtx_);
    LsRequestState state;
    state.hydfs_filename = hydfs_filename;
    state.expected_replicas = replicas;
    state.start_time = std::chrono::steady_clock::now();
    pending_ls_[hydfs_filename] = state;
  }

  // Send FILE_EXISTS_REQUEST to all replicas
  std::string requester_id = std::string(self_id_.host) + ":" + std::string(self_id_.port);
  for (const auto& replica : replicas) {
    FileExistsRequest req;
    req.hydfs_filename = hydfs_filename;
    req.requester_id = requester_id;

    char buffer[8192];
    size_t size = req.serialize(buffer, sizeof(buffer));

    struct sockaddr_in dest_addr;
    socket_.buildServerAddr(dest_addr, replica.host, replica.port);
    sendFileMessage(FileMessageType::FILE_EXISTS_REQUEST, buffer, size, dest_addr);
  }

  // Wait for responses with 3 second timeout
  {
    std::unique_lock<std::mutex> lock(pending_ls_mtx_);
    bool got_all_responses = ls_cv_.wait_for(lock, std::chrono::seconds(3), [&]() {
      auto it = pending_ls_.find(hydfs_filename);
      if (it == pending_ls_.end()) return true;
      return it->second.responses.size() >= replicas.size();
    });

    auto it = pending_ls_.find(hydfs_filename);
    if (it == pending_ls_.end()) {
      std::cout << "❌ LS request cancelled or failed" << std::endl;
      return;
    }

    // Display results
    std::cout << "\n=== LS RESULTS ===" << std::endl;
    std::cout << "Replicas that should store this file (based on hash ring): " << replicas.size() << std::endl;
    std::cout << "Responses received: " << it->second.responses.size() << std::endl;

    if (!got_all_responses) {
      std::cout << "\n⚠ Warning: Timeout waiting for all responses\n" << std::endl;
    }

    bool file_exists_somewhere = false;
    std::cout << "\nReplica Status:\n";
    for (const auto& replica : replicas) {
      uint64_t ring_id = hash_ring_.getNodePosition(replica);
      std::string vm_address = std::string(replica.host) + ":" + std::string(replica.port);

      auto resp_it = it->second.responses.find(vm_address);
      if (resp_it != it->second.responses.end()) {
        const auto& resp = resp_it->second;
        if (resp.exists) {
          file_exists_somewhere = true;
          std::cout << "  ✓ " << vm_address << " (ring ID: " << ring_id << ")"
                    << " - HAS FILE (size: " << resp.file_size << " bytes, last modified: " << resp.version << ")" << std::endl;
        } else {
          std::cout << "  ✗ " << vm_address << " (ring ID: " << ring_id << ")"
                    << " - NO FILE" << std::endl;
        }
      } else {
        std::cout << "  ? " << vm_address << " (ring ID: " << ring_id << ")"
                  << " - NO RESPONSE (timeout or unreachable)" << std::endl;
      }
    }

    std::cout << "\n=== SUMMARY ===" << std::endl;
    if (file_exists_somewhere) {
      std::cout << "✓ File EXISTS in HyDFS" << std::endl;
    } else {
      std::cout << "✗ File DOES NOT EXIST in HyDFS" << std::endl;
    }
    std::cout << "================\n" << std::endl;

    // Clean up
    pending_ls_.erase(it);
  }
}

void FileOperationsHandler::listLocalFiles() {
  // Get HyDFS replica files
  std::vector<std::string> hydfs_files = file_store_.listFiles();

  // Get local cached files
  std::vector<std::string> local_files;
  {
    std::lock_guard<std::mutex> lock(local_cache_mtx_);
    for (const auto& pair : local_file_cache_) {
      local_files.push_back(pair.first);
    }
  }

  // Print VM's ring ID
  uint64_t my_ring_id = hash_ring_.getNodePosition(self_id_);
  std::cout << "\n=== LISTSTORE (VM Ring ID: " << my_ring_id << ") ===" << std::endl;
  std::cout << "Node: " << self_id_.host << ":" << self_id_.port << std::endl;
  std::cout << "========================================" << std::endl;

  // Show local files (from 'get' or test_files/)
  std::cout << "\n📁 LOCAL FILES (available for 'create'):" << std::endl;
  if (local_files.empty()) {
    std::cout << "   (No local files)" << std::endl;
  } else {
    for (const auto& filename : local_files) {
      size_t size = 0;
      {
        std::lock_guard<std::mutex> lock(local_cache_mtx_);
        size = local_file_cache_[filename].size();
      }
      std::cout << "   " << filename << " (" << size << " bytes)" << std::endl;
    }
  }

  // Show HyDFS replica files
  std::cout << "\n💾 HyDFS REPLICAS (stored on this VM):" << std::endl;
  if (hydfs_files.empty()) {
    std::cout << "   (No HyDFS replicas)" << std::endl;
  } else {
    for (const auto& filename : hydfs_files) {
      FileMetadata meta = file_store_.getFileMetadata(filename);
      std::cout << "   " << filename << " (file ID: " << meta.file_id << ", "
                << meta.total_size << " bytes, last modified: " << meta.last_modified_timestamp << ")" << std::endl;
    }
  }

  std::cout << "\nTotals: " << local_files.size() << " local, "
            << hydfs_files.size() << " HyDFS replicas" << std::endl;
  std::cout << "========================================\n" << std::endl;
}

void FileOperationsHandler::catLocalFile(const std::string& local_filename) {
  std::cout << "\n=== CAT LOCAL FILE ===" << std::endl;
  std::cout << "File: " << local_filename << std::endl;

  std::vector<char> data;
  if (!getLocalFile(local_filename, data)) {
    std::cout << "❌ File not found in local cache: " << local_filename << std::endl;
    std::cout << "Hint: Use 'liststore' to see available local files" << std::endl;
    std::cout << "=====================\n" << std::endl;
    return;
  }

  std::cout << "Size: " << data.size() << " bytes" << std::endl;
  std::cout << "---------------------" << std::endl;

  // Print the contents
  std::cout.write(data.data(), data.size());

  // Ensure newline at end
  if (!data.empty() && data.back() != '\n') {
    std::cout << std::endl;
  }

  std::cout << "---------------------" << std::endl;
  std::cout << "=====================\n" << std::endl;
}

bool FileOperationsHandler::getFileFromReplica(const std::string& vm_address,
                                               const std::string& hydfs_filename,
                                               const std::string& local_filename) {
  // Parse VM address (host:port)
  size_t colon_pos = vm_address.find(':');
  if (colon_pos == std::string::npos) {
    std::cout << "Invalid VM address format. Use host:port\n";
    return false;
  }

  std::string host = vm_address.substr(0, colon_pos);
  std::string port = vm_address.substr(colon_pos + 1);

  GetFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.local_filename = local_filename;
  req.client_id = hash_ring_.getNodePosition(self_id_);
  req.last_known_sequence = 0;

  char buffer[8192];
  size_t size = req.serialize(buffer, sizeof(buffer));

  struct sockaddr_in dest_addr;
  socket_.buildServerAddr(dest_addr, host.c_str(), port.c_str());

  sendFileMessage(FileMessageType::GET_REQUEST, buffer, size, dest_addr);
  std::cout << "Get request sent to " << vm_address << "\n";
  return true;
}

// ===== Message Handlers =====

void FileOperationsHandler::handleCreateRequest(const CreateFileRequest& req,
                                                const struct sockaddr_in& sender) {
  // DEBUG: Print to both stdout and log
  std::cout << "\n=== RECEIVED CREATE_REQUEST ===" << std::endl;
  std::cout << "Filename: " << req.hydfs_filename << std::endl;
  std::cout << "Data size: " << req.data_size << " bytes" << std::endl;
  std::cout << "Client ID: " << req.client_id << std::endl;
  logger_.log("RECEIVED CREATE_REQUEST for: " + req.hydfs_filename);

  // Convert client_id from uint64_t to string
  std::string client_id_str = std::to_string(req.client_id);

  // Store the file locally - the client has already sent this to all replicas
  bool success = file_store_.createFile(req.hydfs_filename, req.data, client_id_str);

  if (success) {
    std::cout << "✅ File created successfully: " << req.hydfs_filename << std::endl;
    logger_.log("File created successfully from remote request: " + req.hydfs_filename);
  } else {
    std::cout << "❌ File creation failed (may already exist): " << req.hydfs_filename << std::endl;
    logger_.log("File creation failed (may already exist): " + req.hydfs_filename);
  }
  std::cout << "================================\n" << std::endl;

  // Send response back to the requesting client
  CreateFileResponse resp;
  resp.success = success;
  resp.file_id = FileMetadata::generateFileId(req.hydfs_filename);

  if (!success) {
    resp.error_message = "File already exists";
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::CREATE_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleGetRequest(const GetFileRequest& req,
                                             const struct sockaddr_in& sender) {
  char sender_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(sender.sin_addr), sender_ip, INET_ADDRSTRLEN);

  std::cout << "\n=== REPLICA RECEIVED GET_REQUEST ===" << std::endl;
  std::cout << "File: " << req.hydfs_filename << std::endl;
  std::cout << "From: " << sender_ip << ":" << ntohs(sender.sin_port) << std::endl;
  logger_.log("REPLICA: Received GET_REQUEST for " + req.hydfs_filename);

  GetFileResponse resp;

  if (file_store_.hasFile(req.hydfs_filename)) {
    std::cout << "File found in local store" << std::endl;
    resp.success = true;
    resp.metadata = file_store_.getFileMetadata(req.hydfs_filename);
    resp.blocks = file_store_.getFileBlocks(req.hydfs_filename);
    std::cout << "Metadata shows " << resp.metadata.block_ids.size() << " block IDs" << std::endl;
    std::cout << "Retrieved " << resp.blocks.size() << " blocks" << std::endl;

    // Debug: Show each block
    size_t total_block_size = 0;
    for (size_t i = 0; i < resp.blocks.size(); i++) {
      std::cout << "  Block " << i << ": " << resp.blocks[i].size << " bytes (seq: "
                << resp.blocks[i].sequence_num << ", block_id: " << resp.blocks[i].block_id << ")" << std::endl;
      total_block_size += resp.blocks[i].size;
    }
    std::cout << "Total block data size: " << total_block_size << " bytes" << std::endl;
    std::cout << "Metadata total_size: " << resp.metadata.total_size << " bytes" << std::endl;

    // Check if file is too large for single UDP packet
    if (resp.metadata.total_size > 7000) {  // Conservative limit for 8KB buffer
      std::cout << "⚠️  WARNING: File size (" << resp.metadata.total_size
                << " bytes) exceeds safe UDP limit (8KB buffer)" << std::endl;
      std::cout << "This may cause buffer overflow or packet loss." << std::endl;
    }
  } else {
    std::cout << "❌ File not found in local store" << std::endl;
    resp.success = false;
    resp.error_message = "File not found";
  }

  try {
    // Allocate buffer matching UDP limit (64KB)
    std::vector<char> buffer(UDPSocketConnection::BUFFER_LEN);
    size_t size = resp.serialize(buffer.data(), buffer.size());

    if (size > UDPSocketConnection::BUFFER_LEN) {
      std::cout << "❌ ERROR: Serialized size (" << size << " bytes) exceeds UDP packet limit ("
                << UDPSocketConnection::BUFFER_LEN << " bytes)" << std::endl;

      // Send error response instead
      GetFileResponse error_resp;
      error_resp.success = false;
      error_resp.error_message = "File too large for UDP transfer (max ~7KB)";
      std::vector<char> small_buffer(4096);
      size_t error_size = error_resp.serialize(small_buffer.data(), small_buffer.size());
      sendFileMessage(FileMessageType::GET_RESPONSE, small_buffer.data(), error_size, sender);
    } else {
      sendFileMessage(FileMessageType::GET_RESPONSE, buffer.data(), size, sender);
    }
  } catch (const std::exception& e) {
    std::cout << "❌ ERROR during serialization: " << e.what() << std::endl;

    // Send error response
    GetFileResponse error_resp;
    error_resp.success = false;
    error_resp.error_message = std::string("Serialization error: ") + e.what();
    std::vector<char> error_buffer(4096);
    size_t error_size = error_resp.serialize(error_buffer.data(), error_buffer.size());
    sendFileMessage(FileMessageType::GET_RESPONSE, error_buffer.data(), error_size, sender);
  }

  std::cout << "✅ REPLICA: GET_REQUEST processing completed" << std::endl;
  logger_.log("REPLICA: Completed GET_REQUEST for " + req.hydfs_filename +
              (resp.success ? " [SUCCESS]" : " [FAILED]"));
  std::cout << "====================================\n" << std::endl;
}

void FileOperationsHandler::handleAppendRequest(const AppendFileRequest& req,
                                                const struct sockaddr_in& sender) {
  std::cout << "\n=== COORDINATOR RECEIVED APPEND_REQUEST ===" << std::endl;
  std::cout << "File: " << req.hydfs_filename << std::endl;
  std::cout << "Client ID: " << req.client_id << std::endl;
  std::cout << "Sequence: " << req.sequence_num << std::endl;
  std::cout << "Data size: " << req.data_size << " bytes" << std::endl;

  // Create block
  FileBlock block;
  block.client_id = std::to_string(req.client_id);
  block.sequence_num = req.sequence_num;
  auto now = std::chrono::system_clock::now();
  block.timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  block.data = req.data;
  block.size = req.data.size();
  block.block_id = FileBlock::generateBlockId(block.client_id, block.timestamp, req.sequence_num);

  std::cout << "Generated block ID: " << block.block_id << std::endl;

  // Append locally
  bool success = file_store_.appendBlock(req.hydfs_filename, block);

  if (success) {
    std::cout << "✅ Appended to local store" << std::endl;
    logger_.log("Appended block " + std::to_string(block.block_id) + " to " + req.hydfs_filename);
  } else {
    std::cout << "❌ Failed to append locally" << std::endl;
  }

  AppendFileResponse resp;
  resp.success = success;
  resp.block_id = block.block_id;

  if (!success) {
    resp.error_message = "File not found or append failed";
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::APPEND_RESPONSE, buffer, size, sender);

  // Replicate to other nodes
  if (success) {
    std::vector<NodeId> replicas = hash_ring_.getFileReplicas(req.hydfs_filename, 3);
    std::cout << "Replicating to " << replicas.size() << " replicas..." << std::endl;

    replicateBlock(req.hydfs_filename, block, replicas);
    client_tracker_.recordAppend(block.client_id, req.hydfs_filename, block.block_id);

    std::cout << "✅ COORDINATOR: Append operation completed" << std::endl;
  }

  std::cout << "=========================================\n" << std::endl;
}

void FileOperationsHandler::handleMergeRequest(const MergeFileRequest& req,
                                               const struct sockaddr_in& sender) {
  // TODO: Implement full merge coordination
  // For now, just respond with success
  MergeFileResponse resp;
  resp.success = true;
  resp.new_version = file_store_.getFileMetadata(req.hydfs_filename).version;

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::MERGE_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleLsRequest(const LsFileRequest& req,
                                            const struct sockaddr_in& sender) {
  LsFileResponse resp;
  resp.success = true;
  resp.file_id = FileMetadata::generateFileId(req.hydfs_filename);

  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(req.hydfs_filename, 3);
  for (const auto& replica : replicas) {
    std::stringstream ss;
    ss << replica.host << ":" << replica.port;
    resp.vm_addresses.push_back(ss.str());
    resp.ring_ids.push_back(hash_ring_.getNodePosition(replica));
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::LS_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleListStoreRequest(const ListStoreRequest& /* req */,
                                                   const struct sockaddr_in& sender) {
  ListStoreResponse resp;

  std::vector<std::string> files = file_store_.listFiles();
  for (const auto& filename : files) {
    resp.filenames.push_back(filename);
    resp.file_ids.push_back(file_store_.getFileMetadata(filename).file_id);
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::LISTSTORE_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleFileExistsRequest(const FileExistsRequest& req,
                                                     const struct sockaddr_in& sender) {
  FileExistsResponse resp;
  resp.hydfs_filename = req.hydfs_filename;
  resp.exists = file_store_.hasFile(req.hydfs_filename);

  if (resp.exists) {
    FileMetadata meta = file_store_.getFileMetadata(req.hydfs_filename);
    resp.file_id = meta.file_id;
    resp.file_size = meta.total_size;
    resp.version = meta.last_modified_timestamp;  // Using version field to store timestamp
  } else {
    resp.file_id = 0;
    resp.file_size = 0;
    resp.version = 0;
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::FILE_EXISTS_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleFileExistsResponse(const FileExistsResponse& resp) {
  std::lock_guard<std::mutex> lock(pending_ls_mtx_);

  auto it = pending_ls_.find(resp.hydfs_filename);
  if (it == pending_ls_.end()) {
    // No pending request for this file
    return;
  }

  // Find which replica this response is from by checking against expected replicas
  for (const auto& replica : it->second.expected_replicas) {
    std::string vm_address = std::string(replica.host) + ":" + std::string(replica.port);
    // Store the response (we don't know exact sender, so we match by finding missing responses)
    if (it->second.responses.find(vm_address) == it->second.responses.end()) {
      it->second.responses[vm_address] = resp;
      break;
    }
  }

  // Notify if we got all responses
  if (it->second.responses.size() >= it->second.expected_replicas.size()) {
    ls_cv_.notify_all();
  }
}

void FileOperationsHandler::handleReplicateBlock(const ReplicateBlockMessage& msg,
                                                  const struct sockaddr_in& sender) {
  std::cout << "\n=== RECEIVED REPLICATE_BLOCK ===" << std::endl;
  std::cout << "Filename: " << msg.hydfs_filename << std::endl;
  std::cout << "Block ID: " << msg.block.block_id << std::endl;
  std::cout << "Client ID: " << msg.block.client_id << std::endl;
  std::cout << "Data size: " << msg.block.size << " bytes" << std::endl;

  // Store the replicated block
  bool success = file_store_.appendBlock(msg.hydfs_filename, msg.block);

  if (!success) {
    std::cout << "File doesn't exist, creating new file..." << std::endl;
    // Try to create the file first (in case it doesn't exist yet)
    success = file_store_.createFile(msg.hydfs_filename, msg.block.data, msg.block.client_id);
  }

  if (success) {
    std::cout << "✅ Block replicated successfully" << std::endl;
  } else {
    std::cout << "❌ Block replication FAILED" << std::endl;
  }
  std::cout << "================================\n" << std::endl;

  logger_.log("Replicated block for file: " + msg.hydfs_filename +
              (success ? " [SUCCESS]" : " [FAILED]"));

  // Send acknowledgment back to coordinator
  ReplicateBlockMessage ack_msg;
  ack_msg.hydfs_filename = msg.hydfs_filename;
  ack_msg.block = msg.block;  // Include original block for identification

  char buffer[8192];
  size_t size = ack_msg.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::REPLICATE_ACK, buffer, size, sender);
}

void FileOperationsHandler::handleCollectBlocksRequest(const CollectBlocksRequest& req,
                                                       const struct sockaddr_in& sender) {
  CollectBlocksResponse resp;
  resp.hydfs_filename = req.hydfs_filename;

  if (file_store_.hasFile(req.hydfs_filename)) {
    resp.blocks = file_store_.getFileBlocks(req.hydfs_filename);
    resp.version = file_store_.getFileMetadata(req.hydfs_filename).version;
  }

  char buffer[8192];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::COLLECT_BLOCKS_RESPONSE, buffer, size, sender);
}

void FileOperationsHandler::handleMergeUpdate(const MergeUpdateMessage& msg) {
  // TODO: Apply merged block list
  logger_.log("Received merge update for: " + msg.hydfs_filename);
}

void FileOperationsHandler::handleGetResponse(const GetFileResponse& resp,
                                               const std::string& local_filename) {
  std::cout << "\n=== RECEIVED GET_RESPONSE ===" << std::endl;
  std::cout << "Success: " << (resp.success ? "YES" : "NO") << std::endl;

  if (!resp.success) {
    std::cout << "❌ Error: " << resp.error_message << std::endl;
    std::cout << "============================\n" << std::endl;

    // Signal failure to waiting thread
    std::lock_guard<std::mutex> lock(pending_gets_mtx_);
    auto it = pending_gets_.find(resp.metadata.hydfs_filename);
    if (it != pending_gets_.end()) {
      get_results_[resp.metadata.hydfs_filename] = false;
      get_cv_.notify_all();
    }
    return;
  }

  std::cout << "File: " << resp.metadata.hydfs_filename << std::endl;
  std::cout << "Metadata has " << resp.metadata.block_ids.size() << " block IDs" << std::endl;
  std::cout << "Blocks received: " << resp.blocks.size() << std::endl;
  std::cout << "Metadata total_size: " << resp.metadata.total_size << " bytes" << std::endl;

  // Debug: Show each received block
  for (size_t i = 0; i < resp.blocks.size(); i++) {
    std::cout << "  Received Block " << i << ": " << resp.blocks[i].size << " bytes (seq: "
              << resp.blocks[i].sequence_num << ", block_id: " << resp.blocks[i].block_id << ")" << std::endl;
    std::cout << "    Block data.size(): " << resp.blocks[i].data.size() << " bytes" << std::endl;
    if (!resp.blocks[i].data.empty()) {
      std::cout << "    First 20 chars: ";
      size_t preview_len = std::min(size_t(20), resp.blocks[i].data.size());
      for (size_t j = 0; j < preview_len; j++) {
        char c = resp.blocks[i].data[j];
        if (std::isprint(c)) std::cout << c;
        else std::cout << '.';
      }
      std::cout << std::endl;
    }
  }

  // Check read-my-writes consistency
  std::string client_id = getClientId();
  if (!client_tracker_.satisfiesReadMyWrites(client_id, resp.metadata.hydfs_filename,
                                             resp.metadata.block_ids)) {
    std::cout << "❌ Response does not satisfy read-my-writes consistency" << std::endl;
    std::cout << "Some of your appended blocks are missing from this replica" << std::endl;
    std::cout << "============================\n" << std::endl;

    // Signal failure
    std::lock_guard<std::mutex> lock(pending_gets_mtx_);
    get_results_[resp.metadata.hydfs_filename] = false;
    get_cv_.notify_all();
    return;
  }

  // Assemble file from blocks
  std::vector<char> file_data;
  file_data.reserve(resp.metadata.total_size);

  for (const auto& block : resp.blocks) {
    file_data.insert(file_data.end(), block.data.begin(), block.data.end());
  }

  std::cout << "Assembled file data: " << file_data.size() << " bytes" << std::endl;

  // Store in local cache instead of filesystem
  storeLocalFile(local_filename, file_data);

  std::cout << "✅ File stored in local cache: " << local_filename << std::endl;
  logger_.log("GET_RESPONSE processed successfully for " + resp.metadata.hydfs_filename);
  std::cout << "============================\n" << std::endl;

  // Signal success to waiting thread
  std::lock_guard<std::mutex> lock(pending_gets_mtx_);
  get_results_[resp.metadata.hydfs_filename] = true;
  get_cv_.notify_all();
}

void FileOperationsHandler::handleFileMessage(FileMessageType type, const char* buffer,
                                              size_t buffer_size,
                                              const struct sockaddr_in& sender) {
  char sender_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(sender.sin_addr), sender_ip, INET_ADDRSTRLEN);
  std::cout << "[HANDLE_FILE_MSG] Routing message type " << static_cast<int>(type)
            << " from " << sender_ip << ":" << ntohs(sender.sin_port)
            << " (" << buffer_size << " bytes)" << std::endl;

  try {
    switch (type) {
      case FileMessageType::CREATE_REQUEST: {
        std::cout << "[HANDLE_FILE_MSG] Dispatching to handleCreateRequest" << std::endl;
        CreateFileRequest req = CreateFileRequest::deserialize(buffer, buffer_size);
        handleCreateRequest(req, sender);
        break;
      }
      case FileMessageType::GET_REQUEST: {
        GetFileRequest req = GetFileRequest::deserialize(buffer, buffer_size);
        handleGetRequest(req, sender);
        break;
      }
      case FileMessageType::APPEND_REQUEST: {
        AppendFileRequest req = AppendFileRequest::deserialize(buffer, buffer_size);
        handleAppendRequest(req, sender);
        break;
      }
      case FileMessageType::MERGE_REQUEST: {
        MergeFileRequest req = MergeFileRequest::deserialize(buffer, buffer_size);
        handleMergeRequest(req, sender);
        break;
      }
      case FileMessageType::LS_REQUEST: {
        LsFileRequest req = LsFileRequest::deserialize(buffer, buffer_size);
        handleLsRequest(req, sender);
        break;
      }
      case FileMessageType::LISTSTORE_REQUEST: {
        ListStoreRequest req = ListStoreRequest::deserialize(buffer, buffer_size);
        handleListStoreRequest(req, sender);
        break;
      }
      case FileMessageType::FILE_EXISTS_REQUEST: {
        FileExistsRequest req = FileExistsRequest::deserialize(buffer, buffer_size);
        handleFileExistsRequest(req, sender);
        break;
      }
      case FileMessageType::FILE_EXISTS_RESPONSE: {
        FileExistsResponse resp = FileExistsResponse::deserialize(buffer, buffer_size);
        handleFileExistsResponse(resp);
        break;
      }
      case FileMessageType::REPLICATE_BLOCK: {
        std::cout << "[HANDLE_FILE_MSG] Dispatching to handleReplicateBlock" << std::endl;
        ReplicateBlockMessage msg = ReplicateBlockMessage::deserialize(buffer, buffer_size);
        handleReplicateBlock(msg, sender);
        break;
      }
      case FileMessageType::COLLECT_BLOCKS_REQUEST: {
        CollectBlocksRequest req = CollectBlocksRequest::deserialize(buffer, buffer_size);
        handleCollectBlocksRequest(req, sender);
        break;
      }
      case FileMessageType::MERGE_UPDATE: {
        MergeUpdateMessage msg = MergeUpdateMessage::deserialize(buffer, buffer_size);
        handleMergeUpdate(msg);
        break;
      }
      case FileMessageType::REPLICATE_ACK: {
        // Acknowledgment received - log it
        ReplicateBlockMessage ack_msg = ReplicateBlockMessage::deserialize(buffer, buffer_size);
        logger_.log("Received replication ACK for: " + ack_msg.hydfs_filename);
        break;
      }
      case FileMessageType::CREATE_RESPONSE: {
        CreateFileResponse resp = CreateFileResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] CREATE_RESPONSE received - success: " << resp.success
                  << " file_id: " << resp.file_id << std::endl;
        if (!resp.success) {
          std::cout << "[RESPONSE] Error: " << resp.error_message << std::endl;
        }
        break;
      }
      case FileMessageType::GET_RESPONSE: {
        GetFileResponse resp = GetFileResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] GET_RESPONSE received - success: " << resp.success << std::endl;

        // Find the pending get request to determine local filename
        std::string local_filename;
        {
          std::lock_guard<std::mutex> lock(pending_gets_mtx_);
          auto it = pending_gets_.find(resp.metadata.hydfs_filename);
          if (it != pending_gets_.end()) {
            local_filename = it->second;
          }
        }

        if (!local_filename.empty()) {
          handleGetResponse(resp, local_filename);
        } else {
          std::cout << "[WARNING] Received GET_RESPONSE for non-pending request" << std::endl;
        }
        break;
      }
      case FileMessageType::APPEND_RESPONSE: {
        AppendFileResponse resp = AppendFileResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] APPEND_RESPONSE received - success: " << resp.success
                  << " block_id: " << resp.block_id << std::endl;
        break;
      }
      case FileMessageType::MERGE_RESPONSE: {
        MergeFileResponse resp = MergeFileResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] MERGE_RESPONSE received - success: " << resp.success
                  << " new_version: " << resp.new_version << std::endl;
        break;
      }
      case FileMessageType::LS_RESPONSE: {
        LsFileResponse resp = LsFileResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] LS_RESPONSE received - " << resp.vm_addresses.size() << " replicas" << std::endl;
        break;
      }
      case FileMessageType::LISTSTORE_RESPONSE: {
        ListStoreResponse resp = ListStoreResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] LISTSTORE_RESPONSE received - " << resp.filenames.size() << " files" << std::endl;
        break;
      }
      case FileMessageType::COLLECT_BLOCKS_RESPONSE: {
        CollectBlocksResponse resp = CollectBlocksResponse::deserialize(buffer, buffer_size);
        std::cout << "[RESPONSE] COLLECT_BLOCKS_RESPONSE received - " << resp.blocks.size() << " blocks" << std::endl;
        break;
      }
      default:
        std::cout << "[WARNING] Unknown file message type: " << static_cast<int>(type) << std::endl;
        logger_.log("Unknown file message type: " + std::to_string(static_cast<int>(type)));
        break;
    }
  } catch (const std::exception& e) {
    logger_.log("Error handling file message: " + std::string(e.what()));
  }
}

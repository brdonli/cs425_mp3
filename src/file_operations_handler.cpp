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
      socket_(socket) {}

std::vector<char> FileOperationsHandler::readLocalFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    logger_.log("Error: Could not open local file: " + filename);
    return {};
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    logger_.log("Error: Could not read local file: " + filename);
    return {};
  }

  return buffer;
}

bool FileOperationsHandler::writeLocalFile(const std::string& filename,
                                           const std::vector<char>& data) {
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    logger_.log("Error: Could not open local file for writing: " + filename);
    return false;
  }

  file.write(data.data(), data.size());
  return file.good();
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

  char buffer[65536];
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
  // Read local file
  std::vector<char> data = readLocalFile(local_filename);
  if (data.empty()) {
    std::cout << "Failed to read local file\n";
    return false;
  }

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

  char buffer[65536];
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
      if (writeLocalFile(local_filename, data)) {
        std::cout << "✅ File retrieved successfully: " << hydfs_filename << " -> " << local_filename << std::endl;
        std::cout << "File size: " << data.size() << " bytes" << std::endl;
        logger_.log("GET operation completed for " + hydfs_filename + " (local)");
        std::cout << "========================\n" << std::endl;
        return true;
      }
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

    char buffer[65536];
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
  // Read local file
  std::vector<char> data = readLocalFile(local_filename);
  if (data.empty()) {
    std::cout << "Failed to read local file\n";
    return false;
  }

  // Create append request
  AppendFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.local_filename = local_filename;
  req.client_id = hash_ring_.getNodePosition(self_id_);
  req.sequence_num = getNextSequenceNum(hydfs_filename);
  req.data = data;
  req.data_size = data.size();

  char buffer[65536];
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

  sendFileMessage(FileMessageType::APPEND_REQUEST, buffer, size, dest_addr);
  std::cout << "Append request sent\n";
  return true;
}

bool FileOperationsHandler::mergeFile(const std::string& hydfs_filename) {
  MergeFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.is_coordinator = isCoordinator(hydfs_filename);

  char buffer[65536];
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

  std::cout << "File: " << hydfs_filename << "\n";
  std::cout << "File ID: " << FileMetadata::generateFileId(hydfs_filename) << "\n";
  std::cout << "Stored on " << replicas.size() << " replicas:\n";

  for (const auto& replica : replicas) {
    uint64_t ring_id = hash_ring_.getNodePosition(replica);
    std::cout << "  - " << replica.host << ":" << replica.port << " (ring ID: " << ring_id
              << ")\n";
  }
}

void FileOperationsHandler::listLocalFiles() {
  std::vector<std::string> files = file_store_.listFiles();

  std::cout << "Files stored locally (" << files.size() << "):\n";
  for (const auto& filename : files) {
    FileMetadata meta = file_store_.getFileMetadata(filename);
    std::cout << "  - " << filename << " (file ID: " << meta.file_id << ")\n";
  }
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

  char buffer[65536];
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

  char buffer[65536];
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
    std::cout << "Sending " << resp.blocks.size() << " blocks (" << resp.metadata.total_size << " bytes)" << std::endl;

    // Check if file is too large for single UDP packet
    if (resp.metadata.total_size > 50000) {  // Conservative limit for UDP
      std::cout << "⚠️  WARNING: File size (" << resp.metadata.total_size
                << " bytes) exceeds safe UDP limit" << std::endl;
      std::cout << "This may cause buffer overflow. Consider implementing chunked transfer." << std::endl;
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
      error_resp.error_message = "File too large for UDP transfer (max 64KB)";
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

  // Append locally
  bool success = file_store_.appendBlock(req.hydfs_filename, block);

  AppendFileResponse resp;
  resp.success = success;
  resp.block_id = block.block_id;

  if (!success) {
    resp.error_message = "File not found or append failed";
  }

  char buffer[65536];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::APPEND_RESPONSE, buffer, size, sender);

  // Replicate to other nodes
  if (success) {
    std::vector<NodeId> replicas = hash_ring_.getFileReplicas(req.hydfs_filename, 3);
    replicateBlock(req.hydfs_filename, block, replicas);
    client_tracker_.recordAppend(block.client_id, req.hydfs_filename, block.block_id);
  }
}

void FileOperationsHandler::handleMergeRequest(const MergeFileRequest& req,
                                               const struct sockaddr_in& sender) {
  // TODO: Implement full merge coordination
  // For now, just respond with success
  MergeFileResponse resp;
  resp.success = true;
  resp.new_version = file_store_.getFileMetadata(req.hydfs_filename).version;

  char buffer[65536];
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

  char buffer[65536];
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

  char buffer[65536];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::LISTSTORE_RESPONSE, buffer, size, sender);
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

  char buffer[65536];
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

  char buffer[65536];
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
  std::cout << "Blocks received: " << resp.blocks.size() << std::endl;
  std::cout << "Total size: " << resp.metadata.total_size << " bytes" << std::endl;

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

  // Write to local file
  if (!writeLocalFile(local_filename, file_data)) {
    std::cout << "❌ Failed to write to local file: " << local_filename << std::endl;
    std::cout << "============================\n" << std::endl;

    // Signal failure
    std::lock_guard<std::mutex> lock(pending_gets_mtx_);
    get_results_[resp.metadata.hydfs_filename] = false;
    get_cv_.notify_all();
    return;
  }

  std::cout << "✅ File written successfully to: " << local_filename << std::endl;
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

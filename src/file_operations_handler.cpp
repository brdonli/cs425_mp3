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
  // Create a message buffer with type prefix
  std::array<char, UDPSocketConnection::BUFFER_LEN> msg_buffer;
  msg_buffer[0] = static_cast<uint8_t>(type);
  std::memcpy(msg_buffer.data() + 1, buffer, std::min(buffer_size, msg_buffer.size() - 1));

  ssize_t sent = socket_.write_to_socket(msg_buffer, buffer_size + 1, dest);

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
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(std::atoi(replica.port));
    inet_pton(AF_INET, replica.host, &dest_addr.sin_addr);

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
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(std::atoi(replica.port));
    int inet_result = inet_pton(AF_INET, replica.host, &dest_addr.sin_addr);

    std::cout << "  [SEND] Sending to " << replica.host << ":" << replica.port << std::endl;
    std::cout << "         inet_pton result: " << inet_result << " (1=success, 0=invalid, -1=error)" << std::endl;
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
  // Check if we have it locally
  if (file_store_.hasFile(hydfs_filename)) {
    std::vector<char> data = file_store_.getFile(hydfs_filename);
    if (writeLocalFile(local_filename, data)) {
      std::cout << "File retrieved: " << hydfs_filename << " -> " << local_filename << "\n";
      return true;
    }
  }

  // Otherwise, find a replica
  std::vector<NodeId> replicas = hash_ring_.getFileReplicas(hydfs_filename, 3);
  if (replicas.empty()) {
    std::cout << "No replicas found for file: " << hydfs_filename << "\n";
    return false;
  }

  // Send GET request to first available replica
  GetFileRequest req;
  req.hydfs_filename = hydfs_filename;
  req.local_filename = local_filename;
  req.client_id = hash_ring_.getNodePosition(self_id_);
  req.last_known_sequence = 0;

  char buffer[65536];
  size_t size = req.serialize(buffer, sizeof(buffer));

  struct sockaddr_in dest_addr;
  std::memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(std::atoi(replicas[0].port));
  inet_pton(AF_INET, replicas[0].host, &dest_addr.sin_addr);

  sendFileMessage(FileMessageType::GET_REQUEST, buffer, size, dest_addr);
  std::cout << "Get request sent\n";
  return true;
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
  std::memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(std::atoi(port.c_str()));
  inet_pton(AF_INET, host.c_str(), &dest_addr.sin_addr);

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
  GetFileResponse resp;

  if (file_store_.hasFile(req.hydfs_filename)) {
    resp.success = true;
    resp.metadata = file_store_.getFileMetadata(req.hydfs_filename);
    resp.blocks = file_store_.getFileBlocks(req.hydfs_filename);
  } else {
    resp.success = false;
    resp.error_message = "File not found";
  }

  char buffer[65536];
  size_t size = resp.serialize(buffer, sizeof(buffer));
  sendFileMessage(FileMessageType::GET_RESPONSE, buffer, size, sender);
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
  // Store the replicated block
  bool success = file_store_.appendBlock(msg.hydfs_filename, msg.block);

  if (!success) {
    // Try to create the file first (in case it doesn't exist yet)
    success = file_store_.createFile(msg.hydfs_filename, msg.block.data, msg.block.client_id);
  }

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

void FileOperationsHandler::handleFileMessage(FileMessageType type, const char* buffer,
                                              size_t buffer_size,
                                              const struct sockaddr_in& sender) {
  try {
    switch (type) {
      case FileMessageType::CREATE_REQUEST: {
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
      default:
        logger_.log("Unknown file message type");
        break;
    }
  } catch (const std::exception& e) {
    logger_.log("Error handling file message: " + std::string(e.what()));
  }
}

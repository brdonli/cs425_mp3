#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "client_tracker.hpp"
#include "consistent_hash_ring.hpp"
#include "file_metadata.hpp"
#include "file_store.hpp"
#include "logger.hpp"
#include "message.hpp"
#include "socket.hpp"

/**
 * Handles all file operations for HyDFS
 * Coordinates file creation, retrieval, append, and merge operations
 */
class FileOperationsHandler {
 public:
  FileOperationsHandler(FileStore& file_store, ConsistentHashRing& hash_ring,
                        const NodeId& self_id, Logger& logger, UDPSocketConnection& socket);

  // Core file operations (called from CLI)
  bool createFile(const std::string& local_filename, const std::string& hydfs_filename);
  bool getFile(const std::string& hydfs_filename, const std::string& local_filename);
  bool appendFile(const std::string& local_filename, const std::string& hydfs_filename);
  bool mergeFile(const std::string& hydfs_filename);

  // Response handlers
  void handleGetResponse(const GetFileResponse& resp, const std::string& local_filename);

  // Query operations
  void listFileLocations(const std::string& hydfs_filename);
  void listLocalFiles();
  bool getFileFromReplica(const std::string& vm_address, const std::string& hydfs_filename,
                          const std::string& local_filename);

  // Message handlers (called when receiving network messages)
  void handleCreateRequest(const CreateFileRequest& req, const struct sockaddr_in& sender);
  void handleGetRequest(const GetFileRequest& req, const struct sockaddr_in& sender);
  void handleAppendRequest(const AppendFileRequest& req, const struct sockaddr_in& sender);
  void handleMergeRequest(const MergeFileRequest& req, const struct sockaddr_in& sender);
  void handleLsRequest(const LsFileRequest& req, const struct sockaddr_in& sender);
  void handleListStoreRequest(const ListStoreRequest& req, const struct sockaddr_in& sender);
  void handleFileExistsRequest(const FileExistsRequest& req, const struct sockaddr_in& sender);
  void handleFileExistsResponse(const FileExistsResponse& resp);
  void handleReplicateBlock(const ReplicateBlockMessage& msg, const struct sockaddr_in& sender);
  void handleCollectBlocksRequest(const CollectBlocksRequest& req,
                                  const struct sockaddr_in& sender);
  void handleMergeUpdate(const MergeUpdateMessage& msg);

  // Dispatch incoming file operation messages
  void handleFileMessage(FileMessageType type, const char* buffer, size_t buffer_size,
                         const struct sockaddr_in& sender);

 private:
  FileStore& file_store_;
  ConsistentHashRing& hash_ring_;
  NodeId self_id_;
  Logger& logger_;
  UDPSocketConnection& socket_;
  ClientTracker client_tracker_;

  // Helper: Read local file into buffer
  std::vector<char> readLocalFile(const std::string& filename);

  // Helper: Write buffer to local file
  bool writeLocalFile(const std::string& filename, const std::vector<char>& data);

  // Helper: Get client ID string from NodeId
  std::string getClientId() const;

  // Helper: Get next sequence number for this client
  uint32_t getNextSequenceNum(const std::string& hydfs_filename);

  // Helper: Replicate block to successor nodes
  bool replicateBlock(const std::string& hydfs_filename, const FileBlock& block,
                      const std::vector<NodeId>& replicas);

  // Helper: Send file message to a node
  bool sendFileMessage(FileMessageType type, const char* buffer, size_t buffer_size,
                       const struct sockaddr_in& dest);

  // Helper: Am I the coordinator for this file?
  bool isCoordinator(const std::string& hydfs_filename) const;

  // Tracking sequence numbers per file
  std::unordered_map<std::string, uint32_t> sequence_numbers_;
  std::mutex seq_mtx_;

  // Tracking pending get requests (hydfs_filename -> local_filename)
  std::unordered_map<std::string, std::string> pending_gets_;
  std::mutex pending_gets_mtx_;
  std::condition_variable get_cv_;

  // Results of get requests (hydfs_filename -> success flag)
  std::unordered_map<std::string, bool> get_results_;

  // Tracking pending ls requests
  struct LsRequestState {
    std::string hydfs_filename;
    std::vector<NodeId> expected_replicas;
    std::unordered_map<std::string, FileExistsResponse> responses;  // vm_address -> response
    std::chrono::steady_clock::time_point start_time;
  };
  std::unordered_map<std::string, LsRequestState> pending_ls_;  // hydfs_filename -> state
  std::mutex pending_ls_mtx_;
  std::condition_variable ls_cv_;
};

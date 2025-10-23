#pragma once

#include <memory>
#include <string>

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

  // Pending replication tracking for synchronous operations
  struct PendingReplication {
    std::string filename;
    size_t expected_acks;
    size_t received_acks;
    bool success;
    std::mutex mtx;
    std::condition_variable cv;
  };
  std::unordered_map<std::string, std::shared_ptr<PendingReplication>> pending_replications_;
  std::mutex pending_mtx_;

  // Helper: Wait for replication acknowledgments
  bool waitForReplicationAcks(const std::string& filename, size_t expected_acks, int timeout_ms = 5000);

  // Helper: Record replication acknowledgment
  void recordReplicationAck(const std::string& filename, bool success);
};

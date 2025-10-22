#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <shared_mutex>
#include <string>

#include "message.hpp"

/**
 * Consistent hashing ring implementation for HyDFS
 * Maps both nodes and files to positions on a ring
 */
class ConsistentHashRing {
 public:
  ConsistentHashRing() = default;

  // Add a node to the ring
  void addNode(const NodeId& node);

  // Remove a node from the ring
  void removeNode(const NodeId& node);

  // Get the ring position for a node
  uint64_t getNodePosition(const NodeId& node) const;

  // Get the ring position for a file
  uint64_t getFilePosition(const std::string& filename) const;

  // Get the first n successor nodes for a given position
  std::vector<NodeId> getSuccessors(uint64_t position, int n) const;

  // Get the successor nodes that should store a file
  std::vector<NodeId> getFileReplicas(const std::string& filename, int n) const;

  // Get all nodes currently in the ring (sorted by position)
  std::vector<std::pair<uint64_t, NodeId>> getAllNodes() const;

  // Check if a node exists in the ring
  bool hasNode(const NodeId& node) const;

  // Get the number of nodes in the ring
  size_t size() const;

 private:
  std::map<uint64_t, NodeId> ring;  // sorted map: position -> NodeId
  mutable std::shared_mutex mtx;    // allows multiple readers, single writer

  // Hash function to map data to ring position
  uint64_t hash(const void* data, size_t len) const;

  // Hash a NodeId to get its position
  uint64_t hashNodeId(const NodeId& node) const;

  // Hash a filename to get its position
  uint64_t hashFilename(const std::string& filename) const;
};

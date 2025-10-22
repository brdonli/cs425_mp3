#include "consistent_hash_ring.hpp"

#include <cstring>
#include <functional>
#include <sstream>

void ConsistentHashRing::addNode(const NodeId& node) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  uint64_t position = hashNodeId(node);
  ring[position] = node;
}

void ConsistentHashRing::removeNode(const NodeId& node) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  uint64_t position = hashNodeId(node);
  ring.erase(position);
}

uint64_t ConsistentHashRing::getNodePosition(const NodeId& node) const {
  return hashNodeId(node);
}

uint64_t ConsistentHashRing::getFilePosition(const std::string& filename) const {
  return hashFilename(filename);
}

std::vector<NodeId> ConsistentHashRing::getSuccessors(uint64_t position, int n) const {
  std::shared_lock<std::shared_mutex> lock(mtx);
  std::vector<NodeId> successors;

  if (ring.empty()) {
    return successors;
  }

  // Find the first node at or after the position
  auto it = ring.lower_bound(position);

  // Collect n successors (wrapping around if necessary)
  int collected = 0;
  while (collected < n && collected < static_cast<int>(ring.size())) {
    if (it == ring.end()) {
      it = ring.begin();  // wrap around
    }
    successors.push_back(it->second);
    ++it;
    ++collected;
  }

  return successors;
}

std::vector<NodeId> ConsistentHashRing::getFileReplicas(const std::string& filename,
                                                         int n) const {
  uint64_t file_position = hashFilename(filename);
  return getSuccessors(file_position, n);
}

std::vector<std::pair<uint64_t, NodeId>> ConsistentHashRing::getAllNodes() const {
  std::shared_lock<std::shared_mutex> lock(mtx);
  std::vector<std::pair<uint64_t, NodeId>> nodes;
  for (const auto& entry : ring) {
    nodes.push_back(entry);
  }
  return nodes;
}

bool ConsistentHashRing::hasNode(const NodeId& node) const {
  std::shared_lock<std::shared_mutex> lock(mtx);
  uint64_t position = hashNodeId(node);
  return ring.find(position) != ring.end();
}

size_t ConsistentHashRing::size() const {
  std::shared_lock<std::shared_mutex> lock(mtx);
  return ring.size();
}

uint64_t ConsistentHashRing::hash(const void* data, size_t len) const {
  // Simple hash function using std::hash
  // In production, you might want to use SHA-256 or MD5 for better distribution
  std::string str(static_cast<const char*>(data), len);
  return std::hash<std::string>{}(str);
}

uint64_t ConsistentHashRing::hashNodeId(const NodeId& node) const {
  // Create a unique string representation of the node
  std::stringstream ss;
  ss << node.host << ":" << node.port << ":" << node.time;
  std::string node_str = ss.str();
  return hash(node_str.data(), node_str.length());
}

uint64_t ConsistentHashRing::hashFilename(const std::string& filename) const {
  return hash(filename.data(), filename.length());
}

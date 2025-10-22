#include "node.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "message.hpp"
#include "shared.hpp"
#include "socket.hpp"

Node::Node(const std::string_view& host, const std::string_view& port, NodeId& introducer,
           Logger& logger) :
    socket(host, port), introducer(introducer), mem_list(logger), logger(logger), fd_mode(PINGACK) {
  self = NodeId::createNewNode(host, port);
  mem_list.addNode({self, NodeStatus::ALIVE, FailureDetectionMode::PINGACK, currTime(), 0, 0});
  ring.addNode(self);  // MP3: Add self to ring
  fd_mode = FailureDetectionMode::PINGACK;
  socket.initializeUDPConnection();
  introducer_alive = (std::strcmp(introducer.host, self.host) == 0 &&
                      std::strcmp(introducer.port, self.port) == 0);
}

void Node::handleIncoming() {
  // listens to incoming UDP messages parses it and then calls the respective handle method for it
  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;

  struct sockaddr_in client_addr;
  struct sockaddr_in dest_addr;

  Message message;

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  while (!left) {
    // clear previous values
    std::memset(&client_addr, 0, sizeof(client_addr));
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    buffer.fill(0);

    // read incoming udp data
    ssize_t bytes_read =
        socket.read_from_socket(buffer, UDPSocketConnection::BUFFER_LEN, client_addr);
    if (bytes_read <= 0) continue;

    if (dist(gen) >= drop_rate) {
      message = Message::deserialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);

      // handle messages
      switch (message.type) {
        case MessageType::PING: {
          handlePing(buffer, client_addr, message.messages.at(0));
          break;
        }
        case MessageType::ACK:
          handleAck(message.messages.at(0));
          break;
        case MessageType::GOSSIP: {
          std::vector<MembershipInfo> updates = handleGossip(message);
          sendGossip(buffer, updates);  // notify network about refutation
          break;
        }
        case MessageType::JOIN:
          handleJoin(buffer, client_addr, message.messages.at(0));
          break;
        case MessageType::LEAVE:
          handleLeave(message.messages.at(0));
          break;
        case MessageType::SWITCH:
          handleSwitch(message);
          break;
        default:
          std::cerr << "something has gone fcking wrong" << std::endl;
          break;
      }
    } else {
      logger.log("Dropped incoming message due to drop_rate");
    }
  }
  socket.closeConnection();
}

void Node::handleOutgoing() {
  while (!left) {
    switch (fd_mode) {
      case GOSSIP_WITH_SUSPICION:
      case GOSSIP:
        runGossip(fd_mode == GOSSIP_WITH_SUSPICION);
        std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_FREQ));
        break;
      case PINGACK_WITH_SUSPICION:
      case PINGACK:
        runPingAck(fd_mode == PINGACK_WITH_SUSPICION);
        std::this_thread::sleep_for(std::chrono::seconds(PING_FREQ));
        break;
    }
  }
}

void Node::runPingAck(bool enable_suspicion) {
  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;
  struct sockaddr_in dest_addr;
  std::vector<MembershipInfo> updates;

  std::memset(&dest_addr, 0, sizeof(dest_addr));
  buffer.fill(0);

  std::vector<MembershipInfo> kRandomNeighbors = mem_list.selectKRandom(K_RANDOM, self);
  for (const auto& neighbor : kRandomNeighbors) {
    socket.buildServerAddr(dest_addr, neighbor.node_id.host, neighbor.node_id.port);
    size_t bytes_serialized = sendPing().serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);
    socket.write_to_socket(buffer, bytes_serialized, dest_addr);
  }

  uint32_t curr_time = currTime();
  std::this_thread::sleep_for(std::chrono::seconds(T_TIMEOUT));

  for (const auto& neighbor : kRandomNeighbors) {
    try {
      MembershipInfo latest = mem_list.getNodeInfo(neighbor.node_id);
      if (latest.mode != neighbor.mode) continue;  // mode switched in middle
      uint32_t time_delta = curr_time - latest.local_time;
      if (latest.status == NodeStatus::LEFT && time_delta > T_CLEANUP) {
        mem_list.removeNode(latest.node_id, true);
        ring.removeNode(latest.node_id);  // MP3: Remove from ring
        continue;
      }
      if (updateStatus(neighbor, time_delta, enable_suspicion))
        updates.push_back(mem_list.getNodeInfo(neighbor.node_id));
    } catch (std::runtime_error const&) {
      // node was removed in the middle of the ping ack sequence
      continue;
    }
  }
  sendGossip(buffer, updates);
}

void Node::runGossip(bool enable_suspicion) {
  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;

  buffer.fill(0);

  uint32_t cur_time = currTime();
  std::vector<MembershipInfo> nodes_list = mem_list.copy();
  for (const auto& node : nodes_list) {
    NodeId id = node.node_id;
    uint32_t passed_time = cur_time - node.local_time;
    // if self, update heartbeat count and time
    if (id == self) {
      mem_list.incrementHeartBeatCounter(id);
      continue;
    }

    if (node.status == NodeStatus::ALIVE && passed_time > T_TIMEOUT) {
      // if ALIVE and t_fail time has passed, update status
      if (enable_suspicion) {
        mem_list.updateNodeStatus(id, NodeStatus::SUSPECT);  // if using suspicion: ALIVE --> SUS
      } else {
        mem_list.updateNodeStatus(id, NodeStatus::DEAD);  // if no suspicion: ALIVE --> DEAD
      }
    } else if (node.status == NodeStatus::SUSPECT && passed_time > T_FAIL) {
      mem_list.updateNodeStatus(id, NodeStatus::DEAD);  // SUS --> DEAD
    } else if (node.status == NodeStatus::DEAD && passed_time > T_CLEANUP) {
      mem_list.removeNode(id);  // remove from mem_list
      ring.removeNode(id);  // MP3: Remove from ring
    } else if (node.status == NodeStatus::LEFT && passed_time > T_CLEANUP) {
      mem_list.removeNode(node.node_id, true);
      ring.removeNode(node.node_id);  // MP3: Remove from ring
    }
  }

  std::vector<MembershipInfo> updates = mem_list.copy();
  sendGossip(buffer, updates);
}

bool Node::updateStatus(const MembershipInfo& old_info, const uint32_t time_delta,
                        bool enable_suspicion) {
  bool updated = false;
  NodeId node_id = old_info.node_id;
  MembershipInfo latest_info = mem_list.getNodeInfo(node_id);

  if (latest_info.incarnation > old_info.incarnation) {
    // node increased its incarnation lets update our local mem
    mem_list.updateNodeStatus(node_id, latest_info.status);
    mem_list.updateHeartBeatCounter(node_id, latest_info.heartbeat_counter);
    mem_list.updateIncarnation(node_id, latest_info.incarnation);
    updated = true;
  } else if (latest_info.status != old_info.status) {
    // (only possibility) - think node is SUS/DEAD but actually is ALIVE
    mem_list.updateNodeStatus(node_id, latest_info.status);
    updated = true;
  } else {
    switch (old_info.status) {
      case NodeStatus::ALIVE:
        if (time_delta > T_TIMEOUT) {
          if (enable_suspicion) {
            mem_list.updateNodeStatus(node_id, NodeStatus::SUSPECT);
          } else {
            mem_list.updateNodeStatus(node_id, NodeStatus::DEAD);
          }
          updated = true;
        }
        break;
      case NodeStatus::SUSPECT:
        if (time_delta > T_FAIL) {
          mem_list.updateNodeStatus(node_id, NodeStatus::DEAD);
          updated = true;
        }
        break;
      case NodeStatus::DEAD:
      case NodeStatus::LEFT:
        if (time_delta > T_CLEANUP) {
          mem_list.removeNode(node_id, old_info.status == NodeStatus::LEFT);
          ring.removeNode(node_id);  // MP3: Remove from ring
          updated = true;
        }
        break;
    }
  }
  return updated;
}

void Node::handleJoin(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                      struct sockaddr_in& client_addr, MembershipInfo& new_node) {
  // when introducer sees join adds it to its mem_list and then shares to network
  if (new_node.mode != fd_mode) mem_list.updateMode(new_node.node_id, fd_mode);

  // MP3: Add new node to ring
  ring.addNode(new_node.node_id);

  // send new node current membership list
  const std::vector<MembershipInfo> mem_list_copy = mem_list.copy();
  Message message{MessageType::GOSSIP, static_cast<uint32_t>(mem_list_copy.size()), mem_list_copy};
  std::memset(buffer.data(), 0, UDPSocketConnection::BUFFER_LEN);
  size_t bytes_serialized = message.serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);
  socket.write_to_socket(buffer, bytes_serialized, client_addr);

  // tell other nodes about new node
  std::vector<MembershipInfo> update{new_node};
  sendGossip(buffer, update);
}

void Node::joinNetwork() {
  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;
  struct sockaddr_in dest_addr;
  std::memset(&dest_addr, 0, sizeof(dest_addr));

  socket.buildServerAddr(dest_addr, introducer.host, introducer.port);

  // send ping to make sure introducer is alive
  size_t bytes_serialized = sendPing().serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);
  socket.write_to_socket(buffer, bytes_serialized, dest_addr);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));  // wait for introducer to respond

  if (!introducer_alive) {
    std::cerr << "Introducer might be down or network is congested. Failed to join cluster"
              << std::endl;
    exit(1);
  }

  // send info about self
  Message message{MessageType::JOIN, 1, {}};
  message.messages.push_back(mem_list.getNodeInfo(self));

  bytes_serialized = message.serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);
  ssize_t success = socket.write_to_socket(buffer, bytes_serialized, dest_addr);
  if (success < 0) {
    std::cerr << "Introducer might be down. Failed to join cluster" << std::endl;
    exit(1);
  }
}

void Node::leaveNetwork() {
  mem_list.updateNodeStatus(self, NodeStatus::LEFT);
  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;
  mem_list.incrementIncarnation(self);
  std::vector<MembershipInfo> update{mem_list.getNodeInfo(self)};
  sendGossip(buffer, update, MessageType::LEAVE);

  // giving the cluster time to receive the leave update
  // std::this_thread::sleep_for(std::chrono::seconds(6));

  // stop server
  left = true;
}

void Node::handleLeave(const MembershipInfo& leaving_node) {
  mem_list.updateNodeStatus(leaving_node.node_id, leaving_node.status);
  mem_list.updateIncarnation(leaving_node.node_id, leaving_node.incarnation);
  // mem_list.removeNode(leaving_node.node_id, true);
}

void Node::handlePing(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                      struct sockaddr_in& dest_addr, const MembershipInfo& node) {
  try {
    MembershipInfo local_info = mem_list.getNodeInfo(node.node_id);
    // we have seen this node
    if (node.incarnation > local_info.incarnation) {
      // node increased its incarnation lets update our local mem
      mem_list.updateIncarnation(node.node_id, node.incarnation);
    }
    if (node.status != local_info.status) {
      // the node pinged us with a new status of itself
      mem_list.updateNodeStatus(node.node_id, node.status);
    }
  } catch (std::runtime_error const&) {
    // if we haven't seen this node add it to membership list
    mem_list.addNode(node);
    ring.addNode(node.node_id);  // MP3: Add to ring
  }

  // when a node receives a PING they reply with ACK
  std::memset(buffer.data(), 0, UDPSocketConnection::BUFFER_LEN);
  size_t bytes_serialized = sendAck().serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);
  socket.write_to_socket(buffer, bytes_serialized, dest_addr);
}

Message Node::sendPing() {
  Message message{MessageType::PING, 1, {}};
  const MembershipInfo self_info = mem_list.getNodeInfo(self);
  message.messages.push_back(self_info);
  return message;
}

void Node::handleAck(const MembershipInfo& node) {
  if (!introducer_alive) {
    mem_list.addNode(node);
    ring.addNode(node.node_id);  // MP3: Add to ring
    if (node.mode != fd_mode) {
      mem_list.updateMode(self, node.mode);
      fd_mode = node.mode;
    }
    introducer_alive = true;
  } else {
    mem_list.updateLocalTime(node.node_id);
  }
}

Message Node::sendAck() {
  // when sending ack we want to send information of the node that is replying to the ack
  // this is so the originator of the ping knows who has replied
  Message message{MessageType::ACK, 1, {}};
  const MembershipInfo self_info = mem_list.getNodeInfo(self);
  message.messages.push_back(self_info);
  return message;
}

// cases:
// 1. new node --> add to curr mem_list
// 2. higher incarnation --> accept everything
// 3. same incarnation, same status, > heartbeat --> update heartbeat
// 4. same incarnation, diff status, > heartbeat, curr ALIVE, new SUS --> update mem_list and local
// time
// 5. same incarnation, diff status, > heartbeat, curr SUS, new DEAD --> remove from mem_list
// 6. same incarnation, any status, < heartbeat --> do nothing

std::vector<MembershipInfo> Node::handleGossip(const Message& message) {
  std::vector<MembershipInfo> updates;
  for (const auto& update : message.messages) {
    try {
      MembershipInfo curr_status = mem_list.getNodeInfo(update.node_id);
      if (update.mode != curr_status.mode) continue;  // mode switched in middle
      if (update.incarnation > curr_status.incarnation) {
        // prefer the status of a message with a higher incarnation number
        // case 2: higher incarnation --> accept everything
        mem_list.updateNodeStatus(curr_status.node_id, update.status);
        mem_list.updateHeartBeatCounter(curr_status.node_id, update.heartbeat_counter);
        mem_list.incrementIncarnation(curr_status.node_id);
      } else if ((update.incarnation == curr_status.incarnation) &&
                 (update.heartbeat_counter > curr_status.heartbeat_counter)) {
        // cases 3-5: same incarnation, > heartbeat
        if (update.status == curr_status.status) {
          // case 3: same status --> update heartbeat
          mem_list.updateHeartBeatCounter(curr_status.node_id, update.heartbeat_counter);
        } else {
          if ((update.status == NodeStatus::SUSPECT) && (curr_status.status == NodeStatus::ALIVE)) {
            if (update.node_id == self) {
              // someone thinks we are suspect need to increase incarnation number
              mem_list.updateNodeStatus(self, NodeStatus::ALIVE);
              mem_list.incrementIncarnation(self);
              updates.push_back(mem_list.getNodeInfo(self));
            } else {
              // case 4: diff status, update from ALIVE to SUS --> update mem_list
              mem_list.updateNodeStatus(curr_status.node_id, update.status);
              mem_list.updateHeartBeatCounter(curr_status.node_id, update.heartbeat_counter);
            }
          } else if ((update.status == NodeStatus::DEAD) &&
                     (curr_status.status == NodeStatus::SUSPECT)) {
            if (update.node_id == self) {
              // someone thinks we are dead need to increase incarnation number
              mem_list.updateNodeStatus(self, NodeStatus::ALIVE);
              mem_list.incrementIncarnation(self);
              updates.push_back(mem_list.getNodeInfo(self));
            } else {
              // case 5: diff status, update from SUS to DEAD --> remove from mem_list
              mem_list.removeNode(curr_status.node_id);
              ring.removeNode(curr_status.node_id);  // MP3: Remove from ring
            }
          } else if (update.status == NodeStatus::LEFT && curr_status.status != NodeStatus::LEFT) {
            // we didn't know that node left
            mem_list.removeNode(update.node_id, true);
            ring.removeNode(update.node_id);  // MP3: Remove from ring
            updates.push_back(update);  // want to gossip this info to other nodes
          } else if ((curr_status.status == NodeStatus::SUSPECT ||
                      curr_status.status == NodeStatus::DEAD) &&
                     (update.status == NodeStatus::ALIVE)) {
            // we thought node was suspect or dead but actually is alive
            mem_list.updateNodeStatus(curr_status.node_id, update.status);
            mem_list.updateHeartBeatCounter(curr_status.node_id, update.heartbeat_counter);
          }
        }
      }
    } catch (std::runtime_error const&) {
      // case 1: new node --> add to mem_list
      mem_list.addNode(update);
      ring.addNode(update.node_id);  // MP3: Add new node to ring
    }
  }
  return updates;
}

void Node::sendGossip(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                      std::vector<MembershipInfo>& updates, MessageType message_type) {
  if (updates.size() == 0) return;

  // send message to k peers
  struct sockaddr_in dest_addr;
  std::memset(buffer.data(), 0, UDPSocketConnection::BUFFER_LEN);

  Message message{message_type, static_cast<uint32_t>(updates.size()), updates};
  size_t bytes_serialized = message.serialize(buffer.data(), UDPSocketConnection::BUFFER_LEN);

  std::vector<MembershipInfo> kRandomNeighbors = mem_list.selectKRandom(K_RANDOM, self);
  for (const auto& node : kRandomNeighbors) {
    std::memset(&dest_addr, 0, sizeof(dest_addr));

    socket.buildServerAddr(dest_addr, node.node_id.host, node.node_id.port);
    ssize_t success = socket.write_to_socket(buffer, bytes_serialized, dest_addr);
    if (success < 0) {
      std::cerr << "failed on: " << node.node_id.host << std::endl;
    }
  }
}

void Node::handleSwitch(const Message& message) {
  if (message.num_messages == 0) {
    std::cerr << "Received SWITCH message without mode info\n" << std::endl;
    return;
  }

  FailureDetectionMode mode = message.messages[0].mode;

  logger.log("Received switch request: switching all nodes to mode " +
             std::string(SharedFunctions::modeToStr(mode)));

  // update all nodes in local list
  std::vector<MembershipInfo> nodes_list = mem_list.copy();
  for (const auto& member : nodes_list) mem_list.updateMode(member.node_id, mode);
  std::cout << "\n";
  // update local var
  fd_mode = mode;
}

void Node::switchModes(FailureDetectionMode mode) {
  if (fd_mode == mode) return;

  logger.log("Switching from mode " + std::string(SharedFunctions::modeToStr(fd_mode)) + " to " +
             std::string(SharedFunctions::modeToStr(mode)));
  std::vector<MembershipInfo> nodes_list = mem_list.copy();

  // broadcast to all nodes
  MembershipInfo modeinfo{};
  std::memset(&modeinfo, 0, sizeof(MembershipInfo));
  modeinfo.mode = mode;
  Message message{MessageType::SWITCH, 1, {modeinfo}};

  std::array<char, UDPSocketConnection::BUFFER_LEN> buffer;
  std::memset(buffer.data(), 0, buffer.size());

  size_t bytes_serialized = message.serialize(buffer.data(), buffer.size());

  // change mode in all nodes but self
  for (const auto& member : nodes_list) {
    if (member.node_id == self) continue;
    struct sockaddr_in dest_addr;
    socket.buildServerAddr(dest_addr, member.node_id.host, member.node_id.port);
    socket.write_to_socket(buffer, bytes_serialized, dest_addr);
  }

  // update mode
  for (const auto& member : nodes_list) mem_list.updateMode(member.node_id, mode);
  std::cout << "\n";
  // std::this_thread::sleep_for(std::chrono::seconds(6));  // wait for other nodes to process
  fd_mode = mode;
}

// MP3: List membership with ring IDs
void Node::logMemListWithIds() {
  std::vector<MembershipInfo> members = mem_list.copy();

  // Create a vector of (ring_id, member) pairs
  std::vector<std::pair<uint64_t, MembershipInfo>> sorted_members;
  for (const auto& member : members) {
    uint64_t ring_id = ring.getNodePosition(member.node_id);
    sorted_members.push_back({ring_id, member});
  }

  // Sort by ring ID
  std::sort(sorted_members.begin(), sorted_members.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // Print header
  std::cout << "\n========================================\n";
  std::cout << "Membership List with Ring IDs\n";
  std::cout << "========================================\n";
  std::cout << std::left << std::setw(20) << "Ring ID"
            << std::setw(30) << "Node"
            << std::setw(10) << "Status"
            << std::setw(10) << "Inc"
            << "HB\n";
  std::cout << "----------------------------------------\n";

  // Print each member
  for (const auto& [ring_id, member] : sorted_members) {
    std::cout << std::left
              << std::setw(20) << ring_id
              << std::setw(30) << member.node_id
              << std::setw(10) << to_string(member.status)
              << std::setw(10) << member.incarnation
              << member.heartbeat_counter
              << "\n";
  }

  std::cout << "========================================\n";
  std::cout << "Total nodes: " << sorted_members.size() << "\n";
  std::cout << "Self ring ID: " << ring.getNodePosition(self) << "\n";
  std::cout << "========================================\n\n";
}

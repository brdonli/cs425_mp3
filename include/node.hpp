#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <random>

#include "logger.hpp"
#include "membership_list.hpp"
#include "message.hpp"
#include "shared.hpp"
#include "socket.hpp"
#include "consistent_hash_ring.hpp"

#define HEARTBEAT_FREQ 1  // seconds
#define PING_FREQ 1       // seconds
#define T_TIMEOUT 2       // seconds
#define T_FAIL 2          // seconds
#define T_CLEANUP 2       // seconds
#define K_RANDOM 3

class Node {
 public:
  Node(const std::string_view& host, const std::string_view& port, NodeId& introducer,
       Logger& logger);

  void handleIncoming();
  void handleOutgoing();

  void joinNetwork();
  void leaveNetwork();
  void switchModes(FailureDetectionMode mode);

  inline void logMemList() { mem_list.printMemList(); };
  void logMemListWithIds();  // New function for list_mem_ids
  inline void logSelf() {
    std::stringstream ss;
    ss << mem_list.getNodeInfo(self);
    logger.log(ss.str());
    std::cout << "\n";
  };
  inline void logSuspects() {
    std::cout << "Suspected nodes:\n";
    bool found = false;

    for (const auto& member : mem_list.copy()) {
      if (member.status == NodeStatus::SUSPECT) {
        std::cout << " --> " << member.node_id << " (incarnation=" << member.incarnation << ")\n";
        found = true;
      }
    }

    if (!found) {
      std::cout << " --> None\n";
    }
    std::cout << "\n";
  };
  inline void logProtocol() {
    std::string protocol;
    switch (fd_mode) {
      case FailureDetectionMode::GOSSIP_WITH_SUSPICION:
        protocol = "<gossip, suspect>\n";
        break;
      case FailureDetectionMode::PINGACK_WITH_SUSPICION:
        protocol = "<ping, suspect>\n";
        break;
      case FailureDetectionMode::GOSSIP:
        protocol = "<gossip, nosuspect>\n";
        break;
      case FailureDetectionMode::PINGACK:
        protocol = "<ping, nosuspect>\n";
        break;
    }
    logger.log(protocol);
  }

 private:
  void handleJoin(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                  struct sockaddr_in& client_addr, MembershipInfo& new_node);

  void handleLeave(const MembershipInfo& leaving_node);

  void handlePing(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                  struct sockaddr_in& dest_addr, const MembershipInfo& node);
  Message sendPing();

  void handleAck(const MembershipInfo& node);
  Message sendAck();

  std::vector<MembershipInfo> handleGossip(const Message& messages);
  void sendGossip(std::array<char, UDPSocketConnection::BUFFER_LEN>& buffer,
                  std::vector<MembershipInfo>& updates,
                  MessageType message_type = MessageType::GOSSIP);

  void runPingAck(bool enable_suspicion);
  void runGossip(bool enable_suspicion);
  bool updateStatus(const MembershipInfo& node, const uint32_t time_delta, bool enable_suspicion);

  void handleSwitch(const Message& message);

  std::string modePrefix(FailureDetectionMode mode);

  UDPSocketConnection socket;
  NodeId self, introducer;
  MembershipList mem_list;
  ConsistentHashRing ring;  // MP3: Consistent hash ring
  Logger& logger;
  FailureDetectionMode fd_mode;
  bool left = false, introducer_alive = false;
  float drop_rate = 0.0f;
};
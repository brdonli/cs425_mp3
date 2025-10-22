#include <arpa/inet.h>

#include <cstring>

#include "catch_amalgamated.hpp"
#include "message.hpp"

TEST_CASE("NodeId serialization round-trip") {
  NodeId original = NodeId::createNewNode("localhost", "12345");

  // Calculate actual serialized size
  size_t expected_size = sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time);
  std::vector<char> buffer(expected_size);
  size_t written = original.serialize(buffer.data(), buffer.size());

  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  NodeId deserialized = NodeId::deserialize(recv.data(), recv.size());

  REQUIRE(std::strcmp(original.host, deserialized.host) == 0);
  REQUIRE(std::strcmp(original.port, deserialized.port) == 0);
  REQUIRE(original.time == deserialized.time);
}

TEST_CASE("NodeId buffer too small throws") {
  NodeId node = NodeId::createNewNode("localhost", "12345");
  std::vector<char> small_buffer(10);  // Too small

  REQUIRE_THROWS_AS(node.serialize(small_buffer.data(), small_buffer.size()), std::runtime_error);
}

TEST_CASE("MembershipInfo serialization with heartbeat") {
  MembershipInfo original{};
  original.node_id = NodeId::createNewNode("localhost", "12345");
  original.status = NodeStatus::ALIVE;
  original.incarnation = 0x01020304;
  original.heartbeat_counter = 0x12345678;
  original.mode = FailureDetectionMode::PINGACK;

  size_t expected_size = sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time) +
                         sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t);
  std::vector<char> buffer(expected_size);

  size_t written = original.serialize(buffer.data(), buffer.size(), true);
  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  MembershipInfo deserialized = MembershipInfo::deserialize(recv.data(), recv.size(), true);

  REQUIRE(deserialized.node_id == original.node_id);
  REQUIRE(deserialized.status == original.status);
  REQUIRE(deserialized.mode == original.mode);
  REQUIRE(deserialized.incarnation == original.incarnation);
  REQUIRE(deserialized.heartbeat_counter == original.heartbeat_counter);
}

TEST_CASE("MembershipInfo serialization without heartbeat") {
  MembershipInfo original{};
  original.node_id = NodeId::createNewNode("localhost", "12345");
  original.status = NodeStatus::SUSPECT;
  original.incarnation = 0x0A0B0C0D;
  original.heartbeat_counter = 999;
  original.mode = FailureDetectionMode::GOSSIP;

  size_t expected_size = sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time) +
                         sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t);
  std::vector<char> buffer(expected_size);

  size_t written = original.serialize(buffer.data(), buffer.size());
  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  MembershipInfo deserialized = MembershipInfo::deserialize(recv.data(), recv.size(), false);

  REQUIRE(deserialized.node_id == original.node_id);
  REQUIRE(deserialized.status == original.status);
  REQUIRE(deserialized.mode == original.mode);
  REQUIRE(deserialized.incarnation == original.incarnation);
  REQUIRE(deserialized.heartbeat_counter == 0);  // Should default to 0
}

// Endianness tests removed per request

TEST_CASE("MembershipInfo buffer too small throws") {
  MembershipInfo info{};
  info.node_id = NodeId::createNewNode("localhost", "12345");
  info.status = NodeStatus::ALIVE;
  info.incarnation = 1;
  info.heartbeat_counter = 123;
  info.mode = FailureDetectionMode::PINGACK;

  std::vector<char> small_buffer(10);

  REQUIRE_THROWS_AS(info.serialize(small_buffer.data(), small_buffer.size(), true),
                    std::runtime_error);
}

TEST_CASE("Message serialization with multiple membership infos") {
  Message original{};
  original.type = MessageType::GOSSIP;
  original.num_messages = 2;

  MembershipInfo info1{};
  info1.node_id = NodeId::createNewNode("localhost", "12345");
  info1.status = NodeStatus::ALIVE;
  info1.incarnation = 5;
  info1.heartbeat_counter = 100;
  info1.mode = FailureDetectionMode::PINGACK;

  MembershipInfo info2{};
  info2.node_id = NodeId::createNewNode("localhost", "12345");
  info2.status = NodeStatus::SUSPECT;
  info2.incarnation = 6;
  info2.heartbeat_counter = 200;
  info2.mode = FailureDetectionMode::GOSSIP;

  original.messages = {info1, info2};

  size_t membership_size = sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time) +
                           sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t);
  size_t expected_size = sizeof(uint8_t) + sizeof(uint32_t) + (2 * membership_size);

  std::vector<char> buffer(expected_size);
  size_t written = original.serialize(buffer.data(), buffer.size());
  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  Message deserialized = Message::deserialize(recv.data(), recv.size());

  REQUIRE(deserialized.type == original.type);
  REQUIRE(deserialized.num_messages == original.num_messages);
  REQUIRE(deserialized.messages.size() == 2);
  REQUIRE(deserialized.messages[0].node_id == info1.node_id);
  REQUIRE(deserialized.messages[0].status == info1.status);
  REQUIRE(deserialized.messages[0].mode == info1.mode);
  REQUIRE(deserialized.messages[0].incarnation == info1.incarnation);
  REQUIRE(deserialized.messages[0].heartbeat_counter == info1.heartbeat_counter);
  REQUIRE(deserialized.messages[1].node_id == info2.node_id);
  REQUIRE(deserialized.messages[1].status == info2.status);
  REQUIRE(deserialized.messages[1].mode == info2.mode);
  REQUIRE(deserialized.messages[1].incarnation == info2.incarnation);
  REQUIRE(deserialized.messages[1].heartbeat_counter == info2.heartbeat_counter);
}

TEST_CASE("Message serialization without heartbeat") {
  Message original{};
  original.type = MessageType::PING;
  original.num_messages = 1;

  MembershipInfo info{};
  info.node_id = NodeId::createNewNode("localhost", "12345");
  info.status = NodeStatus::DEAD;
  info.incarnation = 2;
  info.heartbeat_counter = 999;
  info.mode = FailureDetectionMode::PINGACK_WITH_SUSPICION;

  original.messages = {info};

  size_t membership_size = sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time) +
                           sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t);
  size_t expected_size = sizeof(uint8_t) + sizeof(uint32_t) + membership_size;

  std::vector<char> buffer(expected_size);
  size_t written = original.serialize(buffer.data(), buffer.size());
  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  Message deserialized = Message::deserialize(recv.data(), recv.size());

  REQUIRE(deserialized.type == original.type);
  REQUIRE(deserialized.num_messages == original.num_messages);
  REQUIRE(deserialized.messages.size() == 1);
  REQUIRE(deserialized.messages[0].node_id == info.node_id);
  REQUIRE(deserialized.messages[0].status == info.status);
  REQUIRE(deserialized.messages[0].mode == info.mode);
  REQUIRE(deserialized.messages[0].incarnation == info.incarnation);
  REQUIRE(deserialized.messages[0].heartbeat_counter == 0);
}

// Endianness tests removed per request

TEST_CASE("Message buffer too small throws") {
  Message msg{};
  msg.type = MessageType::JOIN;
  msg.num_messages = 1;

  MembershipInfo info{};
  info.node_id = NodeId::createNewNode("localhost", "12345");
  info.status = NodeStatus::ALIVE;
  info.incarnation = 3;
  info.heartbeat_counter = 123;
  info.mode = FailureDetectionMode::GOSSIP;
  msg.messages = {info};

  std::vector<char> small_buffer(10);

  REQUIRE_THROWS_AS(msg.serialize(small_buffer.data(), small_buffer.size()), std::runtime_error);
}

TEST_CASE("Message with empty membership list") {
  Message msg{};
  msg.type = MessageType::LEAVE;
  msg.num_messages = 0;
  msg.messages = {};

  size_t expected_size = sizeof(uint8_t) + sizeof(uint32_t);
  std::vector<char> buffer(expected_size);

  size_t written = msg.serialize(buffer.data(), buffer.size());
  REQUIRE(written == expected_size);

  std::vector<char> recv(buffer.size(), 0);
  std::memcpy(recv.data(), buffer.data(), buffer.size());
  Message deserialized = Message::deserialize(recv.data(), recv.size());

  REQUIRE(deserialized.type == msg.type);
  REQUIRE(deserialized.num_messages == 0);
  REQUIRE(deserialized.messages.empty());
}

TEST_CASE("All message types serialization") {
  std::vector<MessageType> types = {MessageType::PING, MessageType::ACK,   MessageType::GOSSIP,
                                    MessageType::JOIN, MessageType::LEAVE, MessageType::SWITCH};

  for (auto type : types) {
    Message msg{};
    msg.type = type;
    msg.num_messages = 0;
    msg.messages = {};

    std::vector<char> buffer(100);
    size_t written = msg.serialize(buffer.data(), buffer.size());

    std::vector<char> recv(written, 0);
    std::memcpy(recv.data(), buffer.data(), written);
    Message deserialized = Message::deserialize(recv.data(), recv.size());

    REQUIRE(deserialized.type == type);
    REQUIRE(deserialized.num_messages == 0);
  }
}

TEST_CASE("All node statuses serialization") {
  std::vector<NodeStatus> statuses = {NodeStatus::ALIVE, NodeStatus::SUSPECT, NodeStatus::DEAD,
                                      NodeStatus::LEFT};

  for (auto status : statuses) {
    MembershipInfo info{};
    info.node_id = NodeId::createNewNode("localhost", "12345");
    info.status = status;
    info.incarnation = 7;
    info.heartbeat_counter = 123;
    info.mode = FailureDetectionMode::GOSSIP_WITH_SUSPICION;

    std::vector<char> buffer(sizeof(NodeId::host) + sizeof(NodeId::port) + sizeof(NodeId::time) +
                             sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t) +
                             sizeof(uint32_t) + sizeof(uint8_t));
    info.serialize(buffer.data(), buffer.size(), true);

    std::vector<char> recv(buffer.size(), 0);
    std::memcpy(recv.data(), buffer.data(), buffer.size());
    MembershipInfo deserialized = MembershipInfo::deserialize(recv.data(), recv.size(), true);

    REQUIRE(deserialized.status == status);
  }
}

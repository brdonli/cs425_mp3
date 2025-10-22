#include <cstring>
#include <future>
#include <iostream>

#include "logger.hpp"
#include "message.hpp"
#include "node.hpp"

#define INTRODUCER_HOST "localhost"
#define INTRODUCER_PORT "12345"

int main(int argc, char* argv[]) {
  if (argc != 3 && argc != 5) {
    std::cerr << "usage: ./build/main host port [introducer_host introducer_port]" << std::endl;
    exit(1);
  }

  // Use command line arguments for introducer if provided, otherwise use defaults
  const char* introducer_host = (argc == 5) ? argv[3] : INTRODUCER_HOST;
  const char* introducer_port = (argc == 5) ? argv[4] : INTRODUCER_PORT;
  
  NodeId introducer_id = NodeId::createNewNode(introducer_host, introducer_port);

  Logger logger{std::cout};
  Node node(argv[1], argv[2], introducer_id, logger);

  auto incoming_future = std::async(std::launch::async, [&node]() { node.handleIncoming(); });
  auto outgoing_future = std::async(std::launch::async, [&node]() { node.handleOutgoing(); });

  std::string input;
  while (true) {
    std::cin >> input;
    if (input == "list_mem") {
      node.logMemList();
    } else if (input == "list_mem_ids") {
      node.logMemListWithIds();
    } else if (input == "list_self") {
      node.logSelf();
    } else if (input == "join") {
      // introducer cannot join to itself
      if (std::strcmp(argv[1], introducer_host) != 0 || std::strcmp(argv[2], introducer_port) != 0) {
        node.joinNetwork();
      }
      else {
        std::cout << "This node is the introducer and cannot join itself \n\n";
      }
    } else if (input == "leave") {
      node.leaveNetwork();
      break;
    } else if (input == "display_suspects") {
      node.logSuspects();
    } else if (input == "switch") {
      // input: switch gossip|ping suspect|nosuspect
      std::string failure_mode, suspicion_mode;
      std::cin >> failure_mode >> suspicion_mode;
      FailureDetectionMode new_fd_mode;
      bool enable_suspicion = (suspicion_mode == "suspect");
      if (failure_mode == "gossip") {
        new_fd_mode = enable_suspicion ? GOSSIP_WITH_SUSPICION : GOSSIP;
      } else if (failure_mode == "ping") {
        new_fd_mode = enable_suspicion ? PINGACK_WITH_SUSPICION : PINGACK;
      } else {
        std::cerr << "Invalid switch command" << std::endl;
        continue;
      }
      node.switchModes(new_fd_mode);
    } else if (input == "display_protocol") {
      node.logProtocol();
    } else {
      std::cerr << "INVALID COMMAND" << std::endl;
    }
  }

  incoming_future.get();
  outgoing_future.get();
  return 0;
}
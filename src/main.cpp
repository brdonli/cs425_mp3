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
    if (input == "help") {
      std::cout << "\n=== HyDFS Commands ===\n\n";
      std::cout << "File Operations:\n";
      std::cout << "  create <localfile> <hydfsfile>   - Create file in HyDFS from local file\n";
      std::cout << "  get <hydfsfile> <localfile>      - Get file from HyDFS to local file\n";
      std::cout << "  append <localfile> <hydfsfile>   - Append local file to HyDFS file\n";
      std::cout << "  merge <hydfsfile>                - Merge all replicas of a file\n";
      std::cout << "  ls <hydfsfile>                   - List all VMs storing the file\n";
      std::cout << "  store                            - List all files stored on this node\n";
      std::cout << "  getfromreplica <vm:port> <hydfsfile> <localfile>\n";
      std::cout << "                                   - Get file from specific replica\n";
      std::cout << "\nMembership Operations:\n";
      std::cout << "  join                             - Join the network\n";
      std::cout << "  leave                            - Leave the network and exit\n";
      std::cout << "  list_mem                         - List all members\n";
      std::cout << "  list_mem_ids                     - List members with ring IDs\n";
      std::cout << "  list_self                        - Display info about this node\n";
      std::cout << "  display_suspects                 - Show suspected nodes\n";
      std::cout << "  display_protocol                 - Show current failure detection mode\n";
      std::cout << "  switch <gossip|ping> <suspect|nosuspect>\n";
      std::cout << "                                   - Switch failure detection mode\n";
      std::cout << "\nOther:\n";
      std::cout << "  help                             - Show this help message\n";
      std::cout << "\nExamples:\n";
      std::cout << "  create test.txt myfile.txt\n";
      std::cout << "  get myfile.txt downloaded.txt\n";
      std::cout << "  append data.txt myfile.txt\n";
      std::cout << "  ls myfile.txt\n";
      std::cout << "  getfromreplica localhost:12345 myfile.txt local.txt\n";
      std::cout << "\n";
    } else if (input == "list_mem") {
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
    }
    // MP3: File operations commands
    else if (input == "create") {
      std::string local_file, hydfs_file;
      std::cin >> local_file >> hydfs_file;
      node.getFileHandler()->createFile(local_file, hydfs_file);
    } else if (input == "get") {
      std::string hydfs_file, local_file;
      std::cin >> hydfs_file >> local_file;
      node.getFileHandler()->getFile(hydfs_file, local_file);
    } else if (input == "append") {
      std::string local_file, hydfs_file;
      std::cin >> local_file >> hydfs_file;
      node.getFileHandler()->appendFile(local_file, hydfs_file);
    } else if (input == "merge") {
      std::string hydfs_file;
      std::cin >> hydfs_file;
      node.getFileHandler()->mergeFile(hydfs_file);
    } else if (input == "ls") {
      std::string hydfs_file;
      std::cin >> hydfs_file;
      node.getFileHandler()->listFileLocations(hydfs_file);
    } else if (input == "store") {
      node.getFileHandler()->listLocalFiles();
    } else if (input == "getfromreplica") {
      std::string vm_address, hydfs_file, local_file;
      std::cin >> vm_address >> hydfs_file >> local_file;
      node.getFileHandler()->getFileFromReplica(vm_address, hydfs_file, local_file);
    } else {
      std::cerr << "INVALID COMMAND" << std::endl;
    }
  }

  incoming_future.get();
  outgoing_future.get();
  return 0;
}
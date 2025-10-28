#pragma once
#include <netinet/in.h>

#include <array>
#include <string>
#include <string_view>

class UDPSocketConnection {
 public:
  // Buffer size for UDP packets
  // Note: While max UDP datagram is 65KB, large stack allocations cause issues
  // Using 8KB as a practical limit (safe for stack allocation)
  // For larger transfers, implement chunking or use TCP
  static constexpr size_t BUFFER_LEN = 8192;  // 8KB

  UDPSocketConnection(const std::string_view &hostname, const std::string_view &port) :
      fd(-1), hostname(hostname), port(port) {}

  void initializeUDPConnection();

  ssize_t read_from_socket(std::array<char, UDPSocketConnection::BUFFER_LEN> &buffer,
                           const size_t bytes_to_read, struct sockaddr_in &clientAddr) const;

  void buildServerAddr(struct sockaddr_in &addr, const std::string_view &ip,
                       const std::string &port);

  ssize_t write_to_socket(const std::array<char, UDPSocketConnection::BUFFER_LEN> &buffer,
                          const size_t bytes_to_write, const struct sockaddr_in &clientAddr) const;

  ssize_t write_to_socket(const std::string &buffer, const struct sockaddr_in &clientAddr) const;

  void closeConnection() const;

 private:
  void buildAddrHints(struct addrinfo &hints, const bool isServer = false);

  int fd;
  const std::string_view hostname, port;
};

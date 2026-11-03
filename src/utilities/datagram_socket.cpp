#include "utilities/datagram_socket.h"
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Utilities {
struct DatagramSocket::Impl {
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
    Impl() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("Unable to initialise UDP");
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == INVALID_SOCKET) { WSACleanup(); throw std::runtime_error("Unable to open UDP socket"); }
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
            closesocket(socket); WSACleanup(); throw std::runtime_error("Unable to configure UDP socket");
        }
    }
    ~Impl() { closesocket(socket); WSACleanup(); }
#else
    int socket = -1;
    Impl() {
        socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket < 0) throw std::runtime_error("Unable to open UDP socket");
        if (fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK) < 0) {
            close(socket); throw std::runtime_error("Unable to configure UDP socket");
        }
        fcntl(socket, F_SETFD, FD_CLOEXEC);
    }
    ~Impl() { close(socket); }
#endif
    sockaddr_in destination{};
};

DatagramSocket::DatagramSocket(std::uint32_t address, std::uint16_t port) : impl_(std::make_unique<Impl>()) {
    impl_->destination.sin_family = AF_INET;
    impl_->destination.sin_addr.s_addr = htonl(address);
    impl_->destination.sin_port = htons(port);
}
DatagramSocket::~DatagramSocket() = default;
bool DatagramSocket::send(const void* data, std::size_t size) {
    const auto sent = sendto(impl_->socket, static_cast<const char*>(data), static_cast<int>(size), 0,
        reinterpret_cast<const sockaddr*>(&impl_->destination), sizeof(impl_->destination));
    return sent >= 0 && static_cast<std::size_t>(sent) == size;
}
}

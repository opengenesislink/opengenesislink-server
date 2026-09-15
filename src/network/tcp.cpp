#include "opengenesis/network/tcp.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace opengenesis::network {
namespace {
void send_all(const int fd,const std::byte* data,std::size_t size){ while(size){const auto n=::send(fd,data,size,MSG_NOSIGNAL); if(n<0){if(errno==EINTR)continue; throw std::runtime_error(std::string("send: ")+std::strerror(errno));} if(n==0)throw std::runtime_error("socket closed during send"); data+=n; size-=static_cast<std::size_t>(n);} }
void recv_all(const int fd,std::byte* data,std::size_t size){ while(size){const auto n=::recv(fd,data,size,0); if(n<0){if(errno==EINTR)continue; throw std::runtime_error(std::string("recv: ")+std::strerror(errno));} if(n==0)throw std::runtime_error("peer closed connection"); data+=n; size-=static_cast<std::size_t>(n);} }
}
TcpSocket::TcpSocket(const int fd):fd_(fd){} TcpSocket::~TcpSocket(){close();}
TcpSocket::TcpSocket(TcpSocket&& o) noexcept:fd_(o.fd_){o.fd_=-1;} TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept{if(this!=&o){close();fd_=o.fd_;o.fd_=-1;}return *this;}
void TcpSocket::close(){if(fd_>=0){::shutdown(fd_,SHUT_RDWR);::close(fd_);fd_=-1;}}
TcpSocket TcpSocket::connect(const std::string& host,const std::uint16_t port,const std::chrono::milliseconds timeout){
    addrinfo hints{}; hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_UNSPEC; addrinfo* res=nullptr; const auto ps=std::to_string(port); if(::getaddrinfo(host.c_str(),ps.c_str(),&hints,&res)!=0)throw std::runtime_error("getaddrinfo failed for "+host);
    for(auto* p=res;p;p=p->ai_next){const int fd=::socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd<0)continue; timeval tv{static_cast<long>(timeout.count()/1000),static_cast<long>((timeout.count()%1000)*1000)};::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)); if(::connect(fd,p->ai_addr,p->ai_addrlen)==0){::freeaddrinfo(res);return TcpSocket(fd);}::close(fd);} ::freeaddrinfo(res);throw std::runtime_error("connect failed to "+host+":"+std::to_string(port));
}
void TcpSocket::send_frame(const protocol::Frame& frame) const { const auto bytes=protocol::encode(frame); send_all(fd_,bytes.data(),bytes.size()); }
protocol::Frame TcpSocket::receive_frame() const { std::array<std::byte,protocol::kHeaderSize> h{};recv_all(fd_,h.data(),h.size());const std::uint32_t size=(std::to_integer<unsigned>(h[12])<<24U)|(std::to_integer<unsigned>(h[13])<<16U)|(std::to_integer<unsigned>(h[14])<<8U)|std::to_integer<unsigned>(h[15]);if(size>protocol::kMaxPayloadSize)throw std::runtime_error("payload too large");std::vector<std::byte> all(h.begin(),h.end());all.resize(protocol::kHeaderSize+size);if(size)recv_all(fd_,all.data()+protocol::kHeaderSize,size);return protocol::decode(all);}
TcpListener::TcpListener(const std::string& address,const std::uint16_t port,const int backlog){addrinfo hints{};hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_UNSPEC;hints.ai_flags=AI_PASSIVE;addrinfo* res=nullptr;const auto ps=std::to_string(port);if(::getaddrinfo(address.empty()?nullptr:address.c_str(),ps.c_str(),&hints,&res)!=0)throw std::runtime_error("listener getaddrinfo failed");for(auto* p=res;p;p=p->ai_next){fd_=::socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd_<0)continue;int one=1;::setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));if(::bind(fd_,p->ai_addr,p->ai_addrlen)==0&&::listen(fd_,backlog)==0)break;::close(fd_);fd_=-1;}::freeaddrinfo(res);if(fd_<0)throw std::runtime_error("cannot bind listener");}
TcpListener::~TcpListener(){if(fd_>=0)::close(fd_);} std::optional<TcpSocket> TcpListener::accept_for(const std::chrono::milliseconds timeout) const {pollfd p{fd_,POLLIN,0};const int r=::poll(&p,1,static_cast<int>(timeout.count()));if(r==0)return std::nullopt;if(r<0){if(errno==EINTR)return std::nullopt;throw std::runtime_error("poll failed");}const int c=::accept(fd_,nullptr,nullptr);if(c<0){if(errno==EINTR)return std::nullopt;throw std::runtime_error("accept failed");}return TcpSocket(c);}
}

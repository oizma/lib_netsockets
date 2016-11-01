#if defined (_MSC_VER)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> //hostent
#endif

#include <iostream>
#include <cerrno>
#include <string>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "socket.hh"

const int MAXPENDING = 5; // maximum outstanding connection requests

/////////////////////////////////////////////////////////////////////////////////////////////////////
//socket_t::close()
/////////////////////////////////////////////////////////////////////////////////////////////////////

void socket_t::close()
{
#if defined (_MSC_VER)
  closesocket(m_socket);
#else
  ::close(m_socket);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//socket_t::write
/////////////////////////////////////////////////////////////////////////////////////////////////////

void socket_t::write(const void *_buf, int size_buf)
{
  const char *buf = (char*)_buf; // can't do pointer arithmetic on void* 
  int send_size; // size in bytes sent or -1 on error 
  size_t size_left; // size in bytes left to send 
  const int flags = 0;

  size_left = size_buf;

  while (size_left > 0)
  {
    if ((send_size = send(m_socket, buf, size_left, flags)) == -1)
    {
      std::cout << "send error: " << strerror(errno) << std::endl;
    }
    size_left -= send_size;
    buf += send_size;
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//socket_t::read_some
//assumptions: 
//total size to receive is not known
//other end did not close() connection, so not possible to check a zero return value of recv()
/////////////////////////////////////////////////////////////////////////////////////////////////////

int socket_t::read_some(void *buf, int size_buf)
{
  int recv_size; // size in bytes received or -1 on error 
  const int flags = 0;

  if ((recv_size = recv(m_socket, static_cast<char *>(buf), size_buf, flags)) == -1)
  {
    std::cout << "recv error: " << strerror(errno) << std::endl;
  }

  return recv_size;
}

///////////////////////////////////////////////////////////////////////////////////////
//socket_t::read_all
//http://man7.org/linux/man-pages/man2/recv.2.html
//recv() 'size_buf' bytes and save to local FILE
//assumptions: 
//total size to receive is not known
//other end did a close() connection, so it is possible to check a zero return value of recv()
//usage in HTTP
///////////////////////////////////////////////////////////////////////////////////////

int socket_t::read_all(const char *file_name, bool verbose)
{
  int recv_size; // size in bytes received or -1 on error 
  int total_recv_size = 0;
  const int flags = 0;
  const int size_buf = 4096;
  char buf[size_buf];
  FILE *file;

  file = fopen(file_name, "wb");
  while (1)
  {
    if ((recv_size = recv(m_socket, buf, size_buf, flags)) == -1)
    {
      std::cout << "recv error: " << strerror(errno) << std::endl;
    }
    total_recv_size += recv_size;
    if (recv_size == 0)
    {
      std::cout << "all bytes received " << std::endl;
      break;
    }
    if (verbose)
    {
      for (int i = 0; i < recv_size; i++)
      {
        std::cout << buf[i];
      }
    }
    fwrite(buf, recv_size, 1, file);
  }
  fclose(file);
  return total_recv_size;
}

///////////////////////////////////////////////////////////////////////////////////////
//socket_t::read_all
//assumptions: 
//total size to receive is known, size_buf
///////////////////////////////////////////////////////////////////////////////////////

std::string socket_t::read_all(size_t size_read)
{
  const int size_buf = 4096;
  char buf[4096];
  int recv_size; // size in bytes received or -1 on error 
  size_t size_left; // size in bytes left to receive 
  const int flags = 0;

  size_left = size_read;

  std::string str;
  while (size_left > 0)
  {
    if ((recv_size = recv(m_socket, buf, size_buf, flags)) == -1)
    {
      std::cout << "recv error: " << strerror(errno) << std::endl;
    }
    size_left -= recv_size;
    str += std::string(buf);
  }

  return str;
}

///////////////////////////////////////////////////////////////////////////////////////
//socket_t::hostname_to_ip
//The getaddrinfo function provides protocol-independent translation from an ANSI host name to an address
///////////////////////////////////////////////////////////////////////////////////////

int socket_t::hostname_to_ip(const char *host_name, char *ip)
{
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_in *h;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if ((rv = getaddrinfo(host_name, "http", &hints, &servinfo)) != 0)
  {
    return 1;
  }

  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    h = (struct sockaddr_in *) p->ai_addr;
    strcpy(ip, inet_ntoa(h->sin_addr));
  }

  freeaddrinfo(servinfo);
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//socket_t::write
/////////////////////////////////////////////////////////////////////////////////////////////////////

size_t socket_t::write(json_t *json)
{
  char *buf_json = NULL;
  std::string buf_send;
  size_t size_json;

  //get char* from json_t
  buf_json = json_dumps(json, JSON_PRESERVE_ORDER);
  size_json = strlen(buf_json);
  //construct send buffer, adding a header with size in bytes of JSON and # terminator
  buf_send = std::to_string(static_cast<long long unsigned int>(size_json));
  buf_send += "#";
  buf_send += std::string(buf_json);

  this->write(buf_send.data(), buf_send.size());

  free(buf_json);
  return buf_send.size();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//json_socket_t::read
/////////////////////////////////////////////////////////////////////////////////////////////////////

json_t * socket_t::read()
{
  int recv_size; // size in bytes received or -1 on error 
  const int size_buf = 20;
  char buf[size_buf];

  //peek header
  if ((recv_size = recv(m_socket, buf, size_buf, MSG_PEEK)) == -1)
  {
    std::cout << "recv error: " << strerror(errno) << std::endl;
  }

  //get size of JSON message
  std::string str(buf);
  size_t pos = str.find("#");
  std::string str_header(str.substr(0, pos));

  //parse header
  if ((recv_size = recv(m_socket, buf, str_header.size() + 1, 0)) == -1)
  {
    std::cout << "recv error: " << strerror(errno) << std::endl;
  }

  //sanity check
  buf[recv_size - 1] = '\0';
  assert(str_header.compare(buf) == 0);

  size_t size_json = static_cast<size_t>(std::stoull(str_header));
  std::string str_buf = read_all(size_json);
  //terminate buffer with size
  std::string str_json = str_buf.substr(0, size_json);

  //construct JSON
  json_error_t *err = NULL;
  json_t *json = json_loadb(str_json.data(), str_json.size(), JSON_PRESERVE_ORDER, err);
  return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_server_t::tcp_server_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

tcp_server_t::tcp_server_t(const unsigned short server_port)
  : socket_t()
{
#if defined (_MSC_VER)
  WSADATA ws_data;
  if (WSAStartup(MAKEWORD(2, 0), &ws_data) != 0)
  {
    exit(1);
  }
#endif
  sockaddr_in server_addr; // local address

  // create TCP socket for incoming connections
  if ((m_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    std::cout << "socket error: " << std::endl;
    exit(1);
  }

  // construct local address structure
  memset(&server_addr, 0, sizeof(server_addr));     // zero out structure
  server_addr.sin_family = AF_INET;                 // internet address family
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // any incoming interface
  server_addr.sin_port = htons(server_port);        // local port

  // bind to the local address
  if (bind(m_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0)
  {
    //bind error: Permission denied
    //probably trying to bind a port under 1024. These ports usually require root privileges to be bound.
    std::cout << "bind error: " << std::endl;
    exit(1);
  }

  // mark the socket so it will listen for incoming connections
  if (listen(m_socket, MAXPENDING) < 0)
  {
    std::cout << "listen error: " << std::endl;
    exit(1);
  }

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_server_t::~tcp_server_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

tcp_server_t::~tcp_server_t()
{
#if defined (_MSC_VER)
  WSACleanup();
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_server_t::accept
/////////////////////////////////////////////////////////////////////////////////////////////////////

socket_t tcp_server_t::accept_client()
{
  int client_socket; // socket descriptor for client
  sockaddr_in client_addr; // client address
#if defined (_MSC_VER)
  int len_addr; // length of client address data structure
#else
  socklen_t len_addr;
#endif

  // set length of client address structure (in-out parameter)
  len_addr = sizeof(client_addr);

  // wait for a client to connect
  if ((client_socket = accept(m_socket, (struct sockaddr *) &client_addr, &len_addr)) < 0)
  {
    std::cout << "accept error " << std::endl;
  }

  // convert IP addresses from a dots-and-number string to a struct in_addr and back
  char *str_ip = inet_ntoa(client_addr.sin_addr);
  std::cout << "handling client " << str_ip << std::endl;

  socket_t socket(client_socket);
  return socket;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_client_t::tcp_client_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

tcp_client_t::tcp_client_t(const char *host_name, const unsigned short server_port)
  : socket_t(),
  m_server_port(server_port)
{
#if defined (_MSC_VER)
  WSADATA ws_data;
  if (WSAStartup(MAKEWORD(2, 0), &ws_data) != 0)
  {
    exit(1);
  }
#endif

  char server_ip[100];

  //get ip address from hostname
  hostname_to_ip(host_name, server_ip);

  //store
  m_server_ip = server_ip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_client_t::open
/////////////////////////////////////////////////////////////////////////////////////////////////////

void tcp_client_t::open()
{
  struct sockaddr_in server_addr; // server address

  // create a stream socket using TCP
  if ((m_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    std::cout << "socket error: " << std::endl;
    exit(1);
  }

  // construct the server address structure
  memset(&server_addr, 0, sizeof(server_addr)); // zero out structure
  server_addr.sin_family = AF_INET; // internet address family
  server_addr.sin_addr.s_addr = inet_addr(m_server_ip.c_str()); // server IP address
  server_addr.sin_port = htons(m_server_port); // server port

  // establish the connection to the server
  if (connect(m_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
  {
    std::cout << "connect error: " << std::endl;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//tcp_client_t::~tcp_client_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

tcp_client_t::~tcp_client_t()
{
#if defined (_MSC_VER)
  WSACleanup();
#endif
}





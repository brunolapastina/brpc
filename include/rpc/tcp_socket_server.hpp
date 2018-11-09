#pragma once

#include <string>
#include <iomanip>
#include <thread>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "rpc/transport_defs.hpp"
#include "rpc/concurrent_queue.hpp"
#include "msgpack.hpp"

class tcp_socket_server
{
public:
   tcp_socket_server( char const * addr, uint16_t const port )
   {
      struct sockaddr_in my_addr;
      my_addr.sin_family      = AF_INET;
      my_addr.sin_port        = htons(port);
      memset( &(my_addr.sin_zero), 0, 8 );

      if( inet_pton( AF_INET, addr, &(my_addr.sin_addr.s_addr) ) != 1 )
      {
         throw std::invalid_argument ( "Invalid address" );
      }

      _server_fd = socket( AF_INET, SOCK_STREAM, 0 );
      if( _server_fd == -1 )
      {
         throw std::system_error( errno, std::generic_category(), "tcp_socket_server: error creating UNIX socket" );;
      }

      int ret = bind( _server_fd, (struct sockaddr*)&my_addr, sizeof(my_addr) );
      if ( ret == -1 )
      {
         throw std::system_error( errno, std::generic_category(), "tcp_socket_server: bind error" );
      }

      ret = listen( _server_fd, 5 );
      if ( ret == -1 )
      {
         throw std::system_error( errno, std::generic_category(), "tcp_socket_server: listen error" );
      }

      _comm_processor_thrd = std::thread( &tcp_socket_server::comm_processor, this );
   }

   tcp_socket_server( tcp_socket_server&& rhs ) = delete;
   tcp_socket_server& operator=( tcp_socket_server&& rhs ) = delete;
   tcp_socket_server( tcp_socket_server& ) = delete;
   tcp_socket_server& operator=( tcp_socket_server& ) = delete;
   tcp_socket_server( tcp_socket_server const & ) = delete;
   tcp_socket_server& operator=( tcp_socket_server const & ) = delete;

   ~tcp_socket_server()
   {
      _keep_running = false;
      _comm_processor_thrd.join();

      if( _server_fd != -1 )
      {
         close( _server_fd );
      }
   }

   void post( int client_fd, pack_buffer const & data )
   {
      struct pollfd pollfds[1];
      pollfds[0].fd = client_fd;
      pollfds[0].events = POLLOUT;
      pollfds[0].revents = 0;

      for( size_t sent = 0; sent < data.size(); /*no increment*/ )
      {
         int ret = poll( pollfds, 1, 100 );
         if (ret < 0)
         {  // Some error on the poll
            throw std::system_error( errno, std::generic_category(), "write: poll error" );
         }
         else if( ret == 0 )
         {  // Timeout
            std::cout << "write: poll timeout" << std::endl;
         }
         else if( !(pollfds[0].revents & POLLOUT) )
         {  // Some error on the socket
            std::cout << "write: connection closed. Message lost." << std::endl;
            break;
         }
         else
         {
            ret = send( client_fd, &data[sent], (data.size() - sent), 0 );
            if ( ret >= 0 )
            {
               sent += ret;
            }
            else if ( (errno != EAGAIN) && (errno != EINTR) )
            {
               throw std::system_error( errno, std::generic_category(), "write: send error" );
            }
            else
            {
               std::cout << "write: error errno=" << errno << std::endl;
            }
         }

         pollfds[0].revents = 0;
      }
   }

   struct message
   {
      message( int fd, msgpack::object_handle&& obj ) : client(fd), msgpack_data(std::move(obj)) {}
      int client;
      msgpack::object_handle msgpack_data;
   };

   concurrent_queue<message> _message_queue;

private:
   bool  _keep_running = true;
   int _server_fd;
   std::thread _comm_processor_thrd;

   void comm_processor()
   {
      std::cout << "comm_processor: started" << std::endl;

      std::vector<struct pollfd> pollfds;
      pollfds.reserve( 2 );

      struct pollfd pfd;
      pfd.fd = _server_fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      pollfds.push_back( pfd );  // Add the own server to the list

      while( _keep_running )
      {
         int ret = poll( pollfds.data(), pollfds.size(), 1000 );
         if (ret < 0)
         {  // Some error on the poll
            throw std::system_error( errno, std::generic_category(), "comm_processor: poll error" );
         }
         else if( ret > 0 )
         {
            for( auto it = pollfds.begin(); it != pollfds.end(); it++ )
            {
               if( it->fd == _server_fd )
               {  // Treat the server
                  if( it->revents & POLLIN )
                  {
                     struct sockaddr_in cli_addr;
                     socklen_t clilen = sizeof(cli_addr);
                     int client_fd = accept(_server_fd, (struct sockaddr *) &cli_addr, &clilen);
                     if ( (client_fd == -1) )
                     {
                        if( (errno != EAGAIN) && (errno != EINTR) && (errno != EWOULDBLOCK) )
                        {
                           throw std::system_error( errno, std::generic_category(), "comm_processor: accept error" );
                        }
                     }
                     else
                     {
                        struct pollfd pfd;
                        pfd.fd = client_fd;
                        pfd.events = POLLIN;
                        pfd.revents = 0;
                        pollfds.push_back( pfd );  // Add client to the list

                        //std::cout << "Client connected" << std::endl;
                     }
                  }
               }
               else
               {  // Treat the client
                  char read_buf[256];
                  int ret = recv( it->fd, read_buf, sizeof(read_buf), 0 );
                  if ( ret > 0 )
                  {
                     auto message = msgpack::unpack(read_buf, ret);
                     _message_queue.emplace_back( it->fd, std::move(message) );
                     it->revents = 0;
                  }
                  else if( (ret == 0) || (errno == ECONNRESET) )
                  {
                     //std::cout << "Client closed connection" << std::endl;
                     close( it->fd );
                     it = pollfds.erase( it );
                     --it; // The loop will increment it
                  }
                  else if ( ( ret == -1 ) && (errno != EAGAIN) && (errno != EINTR) && (errno != EWOULDBLOCK) )
                  {
                     throw std::system_error( errno, std::generic_category(), "comm_processor: recv error" );
                  }
               }

               it->revents = 0;
            }
         }
      }

      std::cout << "comm_processor: finished" << std::endl;
   }

   /*static void hexdump( char const * data, size_t const len )
   {
      size_t const bytes_per_line = 32;

      std::cout << "* * * * * * * * * * * * * * * * * * * * * HexDump of " << std::setw(4) << len << " bytes * * * * * * * * * * * * * * * * * * * * *\n";
      std::cout << std::hex << std::setw(4) << std::setfill('0') << 0 << '\t';

      for( size_t i = 0; i < len; ++i )
      {
         std::cout << std::setw(2) << static_cast<uint32_t>(static_cast<uint8_t>(data[i])) << ' ';
         if( (i % bytes_per_line) == (bytes_per_line - 1) )
         {
            std::cout << '\n';
            std::cout << std::setw(4) << i+1 << '\t';
         }
      }

      std::cout << std::endl;
   }*/
};

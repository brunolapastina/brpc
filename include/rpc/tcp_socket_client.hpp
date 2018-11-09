#pragma once

#include <string>
#include <iostream>
#include <unistd.h>
#include <poll.h>
#include "rpc/transport_defs.hpp"
#include "rpc/concurrent_queue.hpp"

class tcp_socket_client
{
public:
   tcp_socket_client( char const * addr, uint16_t const port )
   {
      struct sockaddr_in my_addr;
      my_addr.sin_family      = AF_INET;
      my_addr.sin_port        = htons(port);
      memset( &(my_addr.sin_zero), 0, 8 );

      if( inet_pton( AF_INET, addr, &(my_addr.sin_addr.s_addr) ) != 1 )
      {
         throw std::invalid_argument ( "Invalid address" );
      }

      _fd = socket( AF_INET, SOCK_STREAM, 0 );
      if( _fd == -1 )
      {
         throw std::system_error( errno, std::generic_category(), "tcp_socket_client: error creating UNIX socket" );;
      }

      int ret = connect( _fd, reinterpret_cast<sockaddr*>(&my_addr), sizeof(my_addr) );
      if ( ret == -1 )
      {
         throw std::system_error( errno, std::generic_category(), "tcp_socket_client: connect error errno=" + std::to_string(errno) );
      }

      _comm_processor_thrd = std::thread( &tcp_socket_client::comm_processor, this );
   }

   tcp_socket_client( tcp_socket_client&& rhs ) = delete;
   tcp_socket_client& operator=( tcp_socket_client&& rhs ) = delete;
   tcp_socket_client( tcp_socket_client& ) = delete;
   tcp_socket_client& operator=( tcp_socket_client& ) = delete;
   tcp_socket_client( tcp_socket_client const & ) = delete;
   tcp_socket_client& operator=( tcp_socket_client const & ) = delete;

   ~tcp_socket_client()
   {
      _keep_running = false;

      if( _fd != -1 )
      {  // This will force the poll to return, and then the thread will terminate
         shutdown( _fd, SHUT_RDWR );
         close( _fd );
      }

      _comm_processor_thrd.join();
   }

   void post( pack_buffer const & data )
   {
      for( size_t sent = 0; sent < data.size(); /*no increment*/ )
      {
         int ret = send( _fd, &data[sent], (data.size() - sent), 0 );
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
   }

   concurrent_queue<msgpack::object_handle> _message_queue;

private:
   bool _keep_running = true;
   int _fd;
   std::thread _comm_processor_thrd;

   void comm_processor()
   {
      while( _keep_running )
      {
         char read_buf[256];
         int ret = recv( _fd, read_buf, sizeof(read_buf), 0 );
         if ( ret > 0 )
         {
            auto message = msgpack::unpack(read_buf, ret);
            _message_queue.push_back( std::move(message) );
         }
         else if( _keep_running )
         {  // If still running, treat any error that migh have happened
            if( ret == 0 )
            {
               throw std::runtime_error( "comm_processor: Server closed connection" );
            }
            else if ( (errno != EAGAIN) && (errno != EINTR) && (errno != EWOULDBLOCK) )
            {
               throw std::system_error( errno, std::generic_category(), "comm_processor: recv error" );
            }
         }
      }
   }
};

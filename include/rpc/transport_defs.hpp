#pragma once

#include <vector>
#include <msgpack.hpp>

//namespace rpc
//{

enum class rpc_message
{
   request      = 0,
   response     = 1,
   notification = 2
};

MSGPACK_ADD_ENUM(rpc_message);

class pack_buffer : public std::vector<char>
{
public:
   pack_buffer( size_t initsz = MSGPACK_SBUFFER_INIT_SIZE )
   {
      std::vector<char>::reserve( initsz );
   }

   void write(char const * s, size_t n )
   {
      std::vector<char>::insert( std::vector<char>::end(), s, s + n );
   }
};

//};

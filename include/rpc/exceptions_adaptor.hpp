#pragma once

#include <string>
#include <stdexcept>
#include <system_error>
#include <msgpack.hpp>

namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
namespace adaptor {

template <>
struct pack<std::exception>
{
   template <typename Stream>
   msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const std::exception & v) const
   {
      o.pack( v.what() );
      return o;
   }
};

} // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack


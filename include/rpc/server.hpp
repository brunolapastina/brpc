#pragma once

#include <string>
#include <iostream>
#include <unordered_map>
#include <functional>

#include "exceptions.hpp"
#include "rpc/transport_defs.hpp"
#include "rpc/tcp_socket_server.hpp"

#include "rpc/call.h"
#include "rpc/func_traits.h"

namespace rpc
{

class server
{
public:
   server() : _conn( "127.0.0.1", 20000 ) {}

   ~server()
   {
      stop();
   }
   //server(server &&other) noexcept
   //server& operator=(server &&other)

   // Specialization for functions of type void (void)
   template< class Callable,
             typename std::enable_if< std::is_void< typename detail::func_traits<Callable>::result_type >::value >::type* = nullptr ,
             typename std::enable_if< detail::is_zero_arg<Callable>::value >::type* = nullptr>
   void bind ( const std::string & method, Callable func )
   {
      enforce_method_uniqueness( method );
      _binded_funcs.emplace( method, [func](msgpack::object const & params_obj) -> pack_buffer
      {
         enforce_arg_count( 0, params_obj.via.array.size );
         func();
         return pack_buffer();
      });
   }

   // Specialization for functions of type ret_t (void)
   template< class Callable,
             typename std::enable_if< !std::is_void< typename detail::func_traits<Callable>::result_type >::value >::type* = nullptr,
             typename std::enable_if< detail::is_zero_arg<Callable>::value >::type* = nullptr>
   void bind ( const std::string & method, Callable func )
   {
      enforce_method_uniqueness( method );
      _binded_funcs.emplace( method, [func](msgpack::object const & params_obj) -> pack_buffer
      {
         enforce_arg_count( 0, params_obj.via.array.size );

         pack_buffer buffer;
         msgpack::pack( buffer, func() );
         return buffer;
      });
   }

   // Specialization for functions of type void (...)
   template< class Callable,
             typename std::enable_if< std::is_void< typename detail::func_traits<Callable>::result_type >::value >::type* = nullptr ,
             typename std::enable_if< !detail::is_zero_arg<Callable>::value >::type* = nullptr>
   void bind ( const std::string & method, Callable func )
   {
      using args_type = typename detail::func_traits<Callable>::args_type;
      constexpr int args_count = std::tuple_size<args_type>::value;

      enforce_method_uniqueness( method );
      _binded_funcs.emplace( method, [func](msgpack::object const & params_obj) -> pack_buffer
      {
         enforce_arg_count( args_count, params_obj.via.array.size );

         args_type params;
         params_obj.convert(params);

         detail::call(func, params);
         return pack_buffer();
      });
   }

   // Specialization for functions of type ret_t (...)
   template< class Callable,
             typename std::enable_if< !std::is_void< typename detail::func_traits<Callable>::result_type >::value >::type* = nullptr,
             typename std::enable_if< !detail::is_zero_arg<Callable>::value >::type* = nullptr>
   void bind ( const std::string & method, Callable func )
   {
      using args_type = typename detail::func_traits<Callable>::args_type;
      constexpr int args_count = std::tuple_size<args_type>::value;

      enforce_method_uniqueness( method );
      _binded_funcs.emplace( method, [func](msgpack::object const & params_obj) -> pack_buffer
      {
         enforce_arg_count( args_count, params_obj.via.array.size );

         args_type params;
         params_obj.convert(params);

         pack_buffer buffer;
         msgpack::pack( buffer, detail::call(func, params) );
         return buffer;
      });
   }


   /*template< class ret_t, class... Args >
   void bind( std::string const & method, ret_t (*func)(Args...) )
   {
      enforce_method_uniqueness( method );

      _binded_funcs.emplace( method, [func](msgpack::object const & params_obj) -> pack_buffer
      {
         if( sizeof...(Args) != params_obj.via.array.size )
         {
            throw bad_call( "Number of parameters for method dont match" );
         }

         std::tuple<Args...> params;
         params_obj.convert(params);

         pack_buffer buffer;
         msgpack::pack( buffer, detail::call(func, params) );
         return buffer;
      });
   }*/

   void run()
   {
      keep_running_ = true;
      runner_thread();
   }

   void async_run( size_t worker_threads = 1 )
   {
      keep_running_ = true;
      for( size_t i = 0; i < worker_threads; ++i )
      {
         threads_.emplace_back( &server::runner_thread, this );
      }
   }

   void stop()
   {
      keep_running_ = false;
      _conn._message_queue.notify_all();
      for( auto& it : threads_ )
      {
         if( it.joinable() )
         {
            it.join();
         }
      }
   }

private:
   using caller_type = std::function< pack_buffer ( msgpack::object const & ) >;
   std::unordered_map<std::string, caller_type> _binded_funcs;
   tcp_socket_server _conn;
   bool  keep_running_;
   std::vector<std::thread> threads_;

   void enforce_method_uniqueness( std::string const & method ) const
   {
      const auto& it = _binded_funcs.find( method );
      if( it != _binded_funcs.end() )
      {
         throw illegal_bind( "Method " + method + " already binded");
      }
   }

   static inline void enforce_arg_count( const size_t should_be, const size_t received )
   {
      if( should_be != received )
      {
         throw bad_call( "Number of parameters for method dont match" );
      }
   }

   pack_buffer handle_exception( std::exception_ptr eptr ) const
   {
      pack_buffer error_data;

      try
      {
         if (eptr)
         {
            std::rethrow_exception(eptr);
         }
      }
      catch(const std::exception& e)
      {
         msgpack::pack( error_data, e );
      }

      return error_data;
   }

   void runner_thread()
   {
      while( keep_running_ )
      {
         if( ! _conn._message_queue.empty_blocking() )
         {
            tcp_socket_server::message const recv_msg = _conn._message_queue.pop_back();

            // deserialized object is valid during the msgpack::object_handle instance is alive.
            msgpack::object const msg_obj = recv_msg.msgpack_data.get();

            if( msg_obj.via.array.size == 3 )
            {
               std::cout << "NOTIFICATION NOT IMPLEMENTED" << std::endl;
            }
            else if( msg_obj.via.array.size == 4 )
            {
               // convert msgpack::object instance into the original type.
               // if the type is mismatched, it throws msgpack::type_error exception.
               std::tuple< rpc_message, uint32_t, std::string, std::vector<char>> msg_fields;
               msg_obj.convert(msg_fields);
               msgpack::object_handle const params_hndl = msgpack::unpack( std::get<3>(msg_fields).data(), std::get<3>(msg_fields).size() );
               pack_buffer error_data;
               pack_buffer result_data;

               const auto& it = _binded_funcs.find( std::get<2>(msg_fields) );
               if( it != _binded_funcs.end() )
               {
                  try
                  {
                     result_data = it->second( params_hndl.get() );
                  }
                  catch(...)
                  {
                     error_data = handle_exception( std::current_exception() );
                  }
               }

               auto response_fields = std::make_tuple( rpc_message::response, std::get<1>(msg_fields), static_cast<std::vector<char>>(error_data), static_cast<std::vector<char>>(result_data) );

               pack_buffer response_buffer;
               msgpack::pack(response_buffer, response_fields);

               _conn.post( recv_msg.client, response_buffer );
            }
            else
            {
               std::cout << "INVALID MESSAGE FORMAT" << std::endl;
            }
         }
      }
   }
};

};

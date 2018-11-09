#pragma once

#include <utility>
#include <future>
#include <unordered_map>
#include "msgpack.hpp"
#include "transport_defs.hpp"
#include "tcp_socket_client.hpp"

namespace rpc
{

class client
{
public:
   client() : _conn( "127.0.0.1", 20000 ),
              _message_processor_thrd( &client::message_processor, this ),
              _msgid_counter( 0 )
   {
   }

   ~client()
   {
      _keep_running = false;
      std::this_thread::sleep_for( std::chrono::milliseconds(10) );  // This is important to make sure that _message_processor_thrd is locked waiting for the queue
      _conn._message_queue.notify_all();
      _message_processor_thrd.join();
   }

   template< class ret_t, class... Args >
   std::future<ret_t> async_call( std::string const & method, Args&&... args )
   {
      auto parameters = std::make_tuple( std::forward<Args>(args)... );
      pack_buffer buffer;
      msgpack::pack(buffer, parameters);

      std::shared_ptr<std::promise<ret_t>> result_promise( new std::promise<ret_t>()) ;
      uint32_t const msgid = _msgid_counter++;
      _waiting_response.emplace( msgid, [result_promise]( std::exception_ptr & error, std::vector<char> const & result ) mutable -> void
      {
         if( error )
         {
            result_promise->set_exception( error );
         }
         else
         {
            try
            {
               msgpack::object_handle const hndl = msgpack::unpack( result.data(), result.size() );
               msgpack::object const obj = hndl.get();
               result_promise->set_value( obj.as<ret_t>() );
            }
            catch(...)
            {
               result_promise->set_exception( std::current_exception() );
            }
         }
      } );

      post_message( rpc_message::request, msgid, method, buffer );

      return result_promise->get_future();
   }

   template< class ret_t, class... Args >
   ret_t call( std::string const & method, Args&&... args )
   {
      auto result = async_call<ret_t>( method, std::forward<Args>(args)... );
      //result.wait();
      return result.get();
   }

private:
   bool _keep_running = true;
   tcp_socket_client _conn;
   std::thread _message_processor_thrd;
   std::atomic_uint32_t _msgid_counter;

   using notifier_type = std::function< void ( std::exception_ptr &, std::vector<char> const & ) >;
   std::unordered_map<uint32_t, notifier_type> _waiting_response;

   void post_message( rpc_message const & type, uint32_t const msgid, std::string const & method, pack_buffer const & params )
   {
      auto message = std::make_tuple( type, msgid, method, static_cast<std::vector<char>>(params) );

      pack_buffer message_buffer;
      msgpack::pack(message_buffer, message);

      _conn.post( message_buffer );
   }

   void message_processor()
   {
      while( _keep_running )
      {
         if( ! _conn._message_queue.empty_blocking() )
         {
            msgpack::object_handle const recv_msg = _conn._message_queue.pop_back();

            // deserialized object is valid during the msgpack::object_handle instance is alive.
            msgpack::object const msg_obj = recv_msg.get();

            if( msg_obj.via.array.size == 3 )
            {
               std::cout << "NOTIFICATION NOT IMPLEMENTED" << std::endl;
            }
            else if( msg_obj.via.array.size == 4 )
            {
               // convert msgpack::object instance into the original type.
               // if the type is mismatched, it throws msgpack::type_error exception.
               std::tuple< rpc_message, uint32_t, std::vector<char>, std::vector<char>> msg_fields;
               msg_obj.convert(msg_fields);

               std::exception_ptr error;

               if( ! std::get<2>(msg_fields).empty() )
               {  // Exception was thrown
                  msgpack::object_handle const hndl = msgpack::unpack( std::get<2>(msg_fields).data(), std::get<2>(msg_fields).size() );
                  msgpack::object const obj = hndl.get();
                  error = std::make_exception_ptr( std::runtime_error( obj.as<std::string>() ) );
               }

               auto it = _waiting_response.find( std::get<1>(msg_fields) );
               if( it != _waiting_response.end() )
               {
                  it->second( error, std::get<3>(msg_fields) );
                  _waiting_response.erase( std::get<1>(msg_fields) );
               }
               else
               {
                  std::cout << "Could not find message with id " << std::get<1>(msg_fields) << std::endl;
               }
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

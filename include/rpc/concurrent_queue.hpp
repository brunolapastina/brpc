#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

template< class T, class Container = std::deque<T> >
class concurrent_queue : private std::queue< T, Container >
{
public:
   concurrent_queue() : std::queue<T, Container>(Container()) {}
   explicit concurrent_queue( const Container& cont ) : std::queue<T, Container>( cont ) {}

   concurrent_queue( concurrent_queue & ) = delete;
   concurrent_queue( concurrent_queue const & ) = delete;
   concurrent_queue( concurrent_queue && ) = delete;

   ~concurrent_queue() = default;

   concurrent_queue& operator=( const concurrent_queue& other ) = delete;
   concurrent_queue& operator=( concurrent_queue&& other ) = delete;

   void notify_all()
   {
      _cv.notify_all();
   }

   bool empty() const
   {
      return std::queue<T, Container>::empty();
   }

   bool empty_blocking()
   {
      std::unique_lock<std::mutex> lck(_mutex);
      if( std::queue<T, Container>::empty() )
      {
         _cv.wait( lck );
      }
      return std::queue<T, Container>::empty();
   }

   size_t size() const
   {
      return std::queue<T, Container>::size();
   }

   void push_back( const T& value )
   {
      std::unique_lock<std::mutex> lck(_mutex);
      std::queue<T, Container>::push( value );
      _cv.notify_one();
   }

   void push_back( T&& value )
   {
      std::unique_lock<std::mutex> lck(_mutex);
      std::queue<T, Container>::push( std::forward<T>(value) );
      _cv.notify_one();
   }

   template< class... Args >
   void emplace_back( Args&&... args )
   {
      std::unique_lock<std::mutex> lck(_mutex);
      std::queue<T, Container>::emplace( std::forward<Args>(args)... );
      _cv.notify_one();
   }

   T pop_back()
   {
      std::unique_lock<std::mutex> lck(_mutex);

      T ret = std::move(std::queue<T, Container>::front());
      std::queue<T, Container>::pop();

      return ret;
   }

private:
   std::mutex _mutex;
   std::condition_variable _cv;
};

#include <iostream>
#include <vector>
#include <tuple>
#include "rpc/client.hpp"


int main()
{
   rpc::client client;

   std::vector<int> vec{ 5, 6 };

   //for( int i = 0; i < 1000; ++i )
   {
      auto begin = std::chrono::high_resolution_clock::now();
      int ret = client.call<int>( "foo", 1, false, "Hello, World", 3.1415, vec );
      auto end = std::chrono::high_resolution_clock::now();
      std::cout << "Ret = " << ret << " in " << std::chrono::duration_cast<std::chrono::microseconds>( end - begin ).count() << " us" << std::endl;
   }

   {
      auto begin = std::chrono::high_resolution_clock::now();
      try
      {
         int ret = client.call<int>( "foo", 1, true, "Hello, World", 3.1415, vec );
         auto end = std::chrono::high_resolution_clock::now();
         std::cout << "Ret = " << ret << " in " << std::chrono::duration_cast<std::chrono::microseconds>( end - begin ).count() << " us" << std::endl;
      }
      catch( std::exception & e )
      {
         auto end = std::chrono::high_resolution_clock::now();
         std::cout << "Exception: '" << e.what() << "' in " << std::chrono::duration_cast<std::chrono::microseconds>( end - begin ).count() << " us" << std::endl;
      }
   }

   return 0;
}

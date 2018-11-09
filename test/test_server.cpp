#include <thread>
#include <chrono>
#include "rpc/server.hpp"


int foo( int a, bool b, std::string c, double d, std::vector<int> e )
{
   /*std::cout << "foo::int    = " << a << std::endl;
   std::cout << "foo::bool   = " << b << std::endl;
   std::cout << "foo::string = " << c << std::endl;
   std::cout << "foo::double = " << d << std::endl;
   std::cout << "foo::vector = ";
   for( auto const & i : e )
   {
      std::cout << std::to_string(i) << " ";
   }
   std::cout << std::endl;*/

   if( b )
   {
      throw std::runtime_error( "true is not good" );
   }

   return 42;
}

int funcA()
{
   return 123;
}

void funcB( int )
{

}

void funcC()
{

}

int main()
{
   rpc::server server;
   server.bind( "foo", &foo );
   server.bind( "funcA", &funcA );
   server.bind( "funcB", &funcB );
   server.bind( "funcC", &funcC );

   int b;
   server.bind( "funcD", [&]( int a){ b = a+1;} );

   //int tst = 3;
   //auto fn = [&](int a){ return tst+a;};
   //server.bind( "functor", fn );

   server.run();

   return 0;
}

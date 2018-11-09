#pragma once

#include <string>
#include <stdexcept>
#include <system_error>
#include "exceptions_adaptor.hpp"

namespace rpc
{

class bad_call : public std::runtime_error
{
public:
   explicit bad_call(const std::string& what_arg) : std::runtime_error(what_arg) {}
   explicit bad_call(const char* what_arg) : std::runtime_error(what_arg) {}
};

class illegal_bind : public std::runtime_error
{
public:
   explicit illegal_bind(const std::string& what_arg) : std::runtime_error(what_arg) {}
   explicit illegal_bind(const char* what_arg) : std::runtime_error(what_arg) {}
};

};

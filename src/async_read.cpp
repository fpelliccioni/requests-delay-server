#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/config.hpp>
#include <boost/url.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;

using tcp_stream = typename beast::tcp_stream::rebind_executor<
        net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

// constexpr auto use_nothrow_awaitable = net::experimental::as_tuple(net::use_awaitable);

net::awaitable<void> test0(tcp_stream stream) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    co_await http::async_read(stream, buffer, req);
}

net::awaitable<void> test1(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    co_await http::async_read(socket, buffer, req, net::use_awaitable);
}

void test2(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    http::read(socket, buffer, req);
}
int main() {
    return 0;
}

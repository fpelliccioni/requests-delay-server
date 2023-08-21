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
// using tcp_stream = typename beast::tcp_stream::rebind_executor<
//         net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

template <typename Body, typename Allocator>
http::message_generator handle_request(http::request<Body, http::basic_fields<Allocator>>&& req) {
    auto const bad_request = [&req](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    auto const not_found = [&req](beast::string_view target) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    auto const server_error = [&req](beast::string_view what) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    std::cout << "Request: " << req.target() << '\n';

    if (req.method() != http::verb::get && req.method() != http::verb::post) {
        return bad_request("Unknown HTTP-method");
    }

    // Handling /echo
    if (req.target() == "/echo" && req.method() == http::verb::post) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = req.body();
        res.prepare_payload();
        return res;
    }

    // Handling /timestamp
    if (req.target() == "/timestamp" && req.method() == http::verb::get) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = std::ctime(&now_time);
        res.prepare_payload();
        return res;
    }

    // Handling /status
    if (req.target() == "/status" && req.method() == http::verb::get) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = "Server is running smoothly!";
        res.prepare_payload();
        return res;
    }

    // Handling /bigfile
    if (req.target().starts_with("/bigfile")) {
        boost::url_view url = req.target();
        auto params = boost::urls::parse_query(url.encoded_query());
        if ( ! params) {
            return bad_request("Invalid query string");
        }

        if (params->size() != 3) {
            return bad_request("Invalid query string");
        }

        // 1000000, 4096, 50
        size_t total_size = 0;
        size_t chunk_size = 0;
        size_t delay_ms = 0;

        for (auto const& param : *params) {
            if (param.key == "total_size") {
                std::from_chars(param.value.data(), param.value.data() + param.value.size(), total_size);
            } else if (param.key == "chunk_size") {
                std::from_chars(param.value.data(), param.value.data() + param.value.size(), chunk_size);
            } else if (param.key == "delay_ms") {
                std::from_chars(param.value.data(), param.value.data() + param.value.size(), delay_ms);
            }
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 255);

        std::vector<uint8_t> body;
        body.reserve(total_size);

        for (size_t sent = 0; sent < total_size; sent += chunk_size) {
            std::vector<uint8_t> chunk;
            chunk.reserve(chunk_size);
            for(size_t i = 0; i < chunk_size && sent + i < total_size; ++i) {
                chunk.push_back(static_cast<uint8_t>(distrib(gen)));
            }
            body.insert(body.end(), chunk.begin(), chunk.end());

            if (sent + chunk_size < total_size) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }

        http::response<http::vector_body<uint8_t>> res {
            std::piecewise_construct,
            std::make_tuple(std::move(body)),
            std::make_tuple(http::status::ok, req.version())
        };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/octet-stream");
        res.content_length(total_size);
        res.keep_alive(req.keep_alive());
        return res;

    }

    return not_found(req.target());
}

//------------------------------------------------------------------------------

// net::awaitable<void> do_session(tcp_stream stream) {
net::awaitable<void> do_session(tcp::socket socket) {

    beast::flat_buffer buffer;

    // This lambda is used to send messages
    try {
        for(;;) {
            // stream.expires_after(std::chrono::seconds(30));

            http::request<http::string_body> req;
            // co_await http::async_read(stream, buffer, req);
            co_await http::async_read(socket, buffer, req, net::use_awaitable);

            http::message_generator msg = handle_request(std::move(req));
            bool keep_alive = msg.keep_alive();
            // co_await beast::async_write(stream, std::move(msg), net::use_awaitable);
            co_await beast::async_write(socket, std::move(msg), net::use_awaitable);

            if ( ! keep_alive) {
                break;
            }
        }
    } catch (boost::system::system_error & se) {
        if (se.code() != http::error::end_of_stream) {
            throw;
        }
    }

    // Send a TCP shutdown
    beast::error_code ec;
    // stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    socket.shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
    // we ignore the error because the client might have
    // dropped the connection already.
}

//------------------------------------------------------------------------------

net::awaitable<void> do_listen(tcp::endpoint endpoint) {
    auto acceptor = net::use_awaitable.as_default_on(tcp::acceptor(co_await net::this_coro::executor));
    acceptor.open(endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(net::socket_base::max_listen_connections);

    for(;;) {
        boost::asio::co_spawn(
            acceptor.get_executor(),
                // do_session(tcp_stream(co_await acceptor.async_accept())),
                do_session(co_await acceptor.async_accept()),
                [](std::exception_ptr e) {
                    if (e) {
                        try {
                            std::rethrow_exception(e);
                        } catch (std::exception &e) {
                            std::cerr << "Error in session: " << e.what() << "\n";
                        }
                    }
                });
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr <<
            "Usage: server <address> <port> <threads>\n" <<
            "Example:\n" <<
            "    server 0.0.0.0 8080 1\n";
        return EXIT_FAILURE;
    }
    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const threads = std::max<int>(1, std::atoi(argv[3]));

    // The io_context is required for all I/O
    net::io_context ioc{threads};

    // Spawn a listening port
    boost::asio::co_spawn(ioc, do_listen(tcp::endpoint{address, port}), [](std::exception_ptr e) {
        if (e) {
            try {
                std::rethrow_exception(e);
            } catch(std::exception & e) {
                std::cerr << "Error in acceptor: " << e.what() << "\n";
            }
        }
    });

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i) {
        v.emplace_back([&ioc] {
            ioc.run();
        });
    }

    ioc.run();

    return 0;
}

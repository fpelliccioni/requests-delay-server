#include <cstddef>

#include <charconv>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/url.hpp>

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

void handle_request(http::request<http::string_body> const& req,
                    http::response<http::vector_body<uint8_t>>& res)
{
    std::cout << "Request: " << req.target() << '\n';
    if (req.target().starts_with("/bigfile")) {
        boost::url_view url = req.target();
        auto params = boost::urls::parse_query(url.encoded_query());
        if ( ! params) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "text/plain");
            std::string error_msg = "Bad request.";
            res.body().assign(error_msg.begin(), error_msg.end());
            return;
        }

        if (params->size() != 3) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "text/plain");
            std::string error_msg = "Bad request.";
            res.body().assign(error_msg.begin(), error_msg.end());
            return;
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

        res.result(http::status::ok);
        res.set(http::field::server, "Boost Beast Server");
        res.set(http::field::content_type, "application/octet-stream");
        res.content_length(total_size);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 255);

        for (size_t sent = 0; sent < total_size; sent += chunk_size) {
            std::vector<uint8_t> chunk;
            chunk.reserve(chunk_size);
            for(size_t i = 0; i < chunk_size && sent + i < total_size; ++i) {
                chunk.push_back(static_cast<uint8_t>(distrib(gen)));
            }
            res.body().insert(res.body().end(), chunk.begin(), chunk.end());

            if (sent + chunk_size < total_size) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }
    } else {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        // res.body() = "The resource was not found.";
        std::string error_msg = "The resource was not found.";
        res.body().assign(error_msg.begin(), error_msg.end());
    }
}

int main() {
    boost::asio::io_context ioc;
    tcp::acceptor acceptor(ioc, {tcp::v4(), 8080});

    for(;;) {
        tcp::socket socket(ioc);
        acceptor.accept(socket);

        http::request<http::string_body> req;
        boost::beast::flat_buffer buffer;
        http::read(socket, buffer, req);

        // http::response<http::string_body> res;
        http::response<http::vector_body<uint8_t>> res;
        handle_request(req, res);

        http::write(socket, res);
    }

    return 0;
}

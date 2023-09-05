#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/json.hpp>

#include <boost/config.hpp>
#include <boost/url.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;
// using tcp_stream = typename beast::tcp_stream::rebind_executor<
//         net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

std::unordered_map<std::string, std::string> parse_form_data(const std::string& body) {
    std::unordered_map<std::string, std::string> data;
    std::istringstream stream(body);
    std::string pair;

    while (std::getline(stream, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            data[key] = value;
        }
    }
    return data;
}

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

    // if (req.method() != http::verb::get && req.method() != http::verb::post) {
    //     return bad_request("Unknown HTTP-method");
    // }

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



    // connection.cpp
    // Handling /headers
    if (req.target() == "/headers" && req.method() == http::verb::get) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());

        boost::json::object headers_obj;
        for (const auto& header : req) {
            headers_obj[header.name_string()] = header.value();
        }
        boost::json::object response_obj;
        response_obj["headers"] = headers_obj;

        res.body() = boost::json::serialize(response_obj);
        res.prepare_payload();
        return res;
    }

    // get-redirect
    if (req.target().starts_with("/redirect-to?url=")) {
        http::response<http::string_body> res{http::status::found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);

        std::string target_url = req.target().substr(18);
        if (target_url == "%2Fget") {
            target_url = "/get";  // ????
        }

        res.set(http::field::location, target_url);

        if (target_url == "/get") {
            res.set(http::field::content_type, "application/json");
            boost::json::object headers_obj;

            for (const auto& header : req) {
                headers_obj[header.name_string()] = header.value();
            }
            boost::json::object response_obj;
            response_obj["headers"] = headers_obj;
            res.body() = boost::json::serialize(response_obj);
        }

        res.prepare_payload();
        return res;
    }

    // too-many-redirects
    if (req.target().starts_with("/redirect/")) {
        http::response<http::string_body> res{http::status::found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);

        std::string redirect_count_str = req.target().substr(10);
        int redirect_count = std::atoi(redirect_count_str.c_str());

        if (redirect_count > 0) {
            --redirect_count;

            std::string next_redirect = "/redirect/" + std::to_string(redirect_count);
            res.set(http::field::location, next_redirect);
        } else {
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            boost::json::object response_obj;
            response_obj["message"] = "Final destination reached!";
            res.body() = boost::json::serialize(response_obj);
        }

        res.prepare_payload();
        return res;
    }

    // download
    if (req.target() == "/image") {
        std::filesystem::path image_path = "requests-test.png";   //TODO

        if ( ! std::filesystem::exists(image_path)) {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/plain");
            res.body() = "File not found";
            res.prepare_payload();

            return res;
        }

        std::ifstream file(image_path.string(), std::ios::binary);
        std::vector<char> file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        http::response<http::vector_body<char>> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "image/png");
        res.content_length(file_content.size());
        res.body() = file_content;

        return res;
    }

    // download-redirect
    if (req.target() == "/redirect-to?url=%2Fimage") {
        http::response<http::string_body> res{http::status::found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::location, "/image");
        return res;
    }

    // delete
    if (req.method() == http::verb::delete_ && req.target() == "/delete") {
        boost::json::value val = boost::json::parse(req.body());
        boost::json::object& obj = val.as_object();

        std::string test_key;
        if (obj.contains("test-key")) {
            test_key = obj["test-key"].as_string().c_str();
        }

        if (test_key == "test-value") {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status": "success"})";
            res.prepare_payload();
            return res;
        } else {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status": "failure"})";
            res.prepare_payload();
            return res;
        }
    }

    // patch-json
    if (req.method() == http::verb::patch && req.target() == "/patch") {
        std::string body_str(req.body().begin(), req.body().end());
        boost::json::value jv = boost::json::parse(body_str);
        boost::json::object obj = jv.as_object();

        if (obj["test-key"].as_string() == "test-value") {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status": "success"})";
            res.prepare_payload();

            return res;
        } else {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status": "failure"})";
            res.prepare_payload();

            return res;
        }
    }

    // patch-form
    if (req.method() == http::verb::patch && req.target() == "/patch" &&
        req[http::field::content_type] == "application/x-www-form-urlencoded") {
        std::string body_str(req.body().begin(), req.body().end());
        std::vector<std::string> tokens;
        boost::split(tokens, body_str, boost::is_any_of("&"));

        boost::json::object obj;
        for (const auto& token : tokens) {
            std::vector<std::string> kv;
            boost::split(kv, token, boost::is_any_of("="));
            if (kv.size() == 2) {
                obj[kv[0]] = kv[1];
            }
        }

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/x-www-form-urlencoded");
        std::string serialized_json = boost::json::serialize(boost::json::value_from(obj));
        res.body() = R"({"status": "success", "form": )" + serialized_json + "}";
        res.prepare_payload();

        return res;
    }

    // put-form
    if (req.method() == http::verb::put && req.target() == "/put" &&
        req[http::field::content_type] == "application/x-www-form-urlencoded") {

        auto form_data = parse_form_data(req.body());

        if (form_data["foo"] == "42" && form_data["bar"] == "21" && form_data["foo bar"] == "23") {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/x-www-form-urlencoded");
            res.body() = "foo=42&bar=21&foo%20bar=23";
            res.prepare_payload();
            return res;
        } else {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/plain");
            res.body() = "Invalid form data";
            res.prepare_payload();
            return res;
        }
    }

    // put-json
    if (req.method() == http::verb::put && req.target() == "/put" &&
        req[http::field::content_type] == "application/json") {

        boost::json::value json_value = boost::json::parse(req.body());

        if (json_value.is_object() &&
            json_value.as_object().contains("test-key") &&
            json_value.as_object()["test-key"] == "test-value") {

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status": "success"})";
            res.prepare_payload();

            return res;
        }
    }

    // post-form
    if (req.method() == http::verb::post && req.target() == "/post" &&
        req[http::field::content_type] == "application/x-www-form-urlencoded") {

        auto form_data = parse_form_data(req.body());

        if (form_data["foo"] == "42" && form_data["bar"] == "21" && form_data["foo bar"] == "23") {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/x-www-form-urlencoded");
            res.body() = "foo=42&bar=21&foo%20bar=23";
            res.prepare_payload();
            return res;
        } else {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.body() = "Invalid form data";
            res.prepare_payload();
            return res;
        }
    }

    // post-json
    if (req.method() == http::verb::post && req.target() == "/post" &&
        req[http::field::content_type] == "application/json") {

        auto json_data = boost::json::parse(req.body());

        if (json_data.at("test-key").as_string() == "test-value") {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"message":"Data received"})";
            res.prepare_payload();
            return res;
        } else {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.body() = R"({"error":"Invalid JSON data"})";
            res.prepare_payload();
            return res;
        }
    }

    // get
    if (req.method() == http::verb::get && req.target() == "/get") {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");

        boost::json::object headers_json;
        for (const auto& header : req.base()) {
            headers_json[header.name_string()] = header.value();
        }

        boost::json::object response_body;
        response_body["headers"] = headers_json;

        res.body() = boost::json::serialize(response_body);
        res.prepare_payload();
        return res;
    }


    // cookie.cpp
    if (req.target().starts_with("/cookies/set?")) {
        auto query = req.target().substr(13);

        auto eq_pos = query.find("=");
        auto cookie_name = query.substr(0, eq_pos);
        auto cookie_value = query.substr(eq_pos + 1);

        // Set the cookie
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::set_cookie, std::string(cookie_name) + "=" + std::string(cookie_value));

        res.result(http::status::ok);
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"cookies":{"cookie-1":"foo","cookie-2":"bar"}})";
        res.prepare_payload();
        return res;
    }

    if (req.target().starts_with("/cookies/delete?")) {
        auto query = req.target().substr(16);

        auto eq_pos = query.find("=");
        auto cookie_name = query.substr(0, eq_pos);

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::set_cookie, std::string(cookie_name) + "=; Max-Age=0");

        res.result(http::status::ok);
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"deleted":")" + std::string(cookie_name) + R"("})";        //TODO: use body() +=   everywhere
        res.prepare_payload();
        return res;
    }


    if (req.target() == "/cookies") {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.result(http::status::ok);
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"cookies":{}})";
        res.prepare_payload();
        return res;
    }
    return not_found(req.target());
}

//------------------------------------------------------------------------------

// net::awaitable<void> do_session(tcp_stream stream) {
net::awaitable<void> do_session(tcp::socket socket) {

    beast::flat_buffer buffer;

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

    beast::error_code ec;
    // stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    socket.shutdown(tcp::socket::shutdown_send, ec);
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

    net::io_context ioc{threads};

    boost::asio::co_spawn(ioc, do_listen(tcp::endpoint{address, port}), [](std::exception_ptr e) {
        if (e) {
            try {
                std::rethrow_exception(e);
            } catch(std::exception & e) {
                std::cerr << "Error in acceptor: " << e.what() << "\n";
            }
        }
    });

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

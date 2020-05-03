//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: HTTP server, asynchronous
//
//------------------------------------------------------------------------------

#include <boost_beast_server/http_server.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace server {
namespace{

// Report a failure
void fail(beast::error_code ec, char const* what) noexcept
{
    std::cerr << what << ": " << ec.message() << "\n";
}
}

void run_io_service(io_context& ioc, const unsigned int thread_num)
{
    std::vector<std::thread> v;
    v.reserve(thread_num - 1);
    // main thead分引く
    for (unsigned int i = 0; i < thread_num - 1; ++i) {
        v.emplace_back([&ioc] { ioc.run(); });
    }
    ioc.run();
}
http::response<http::string_body> create_text_responce(
    const request& req, http::status status, beast::string_param content_type, std::string body
)
{
    http::response<http::string_body> res{ status, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}
namespace detail {
// Take ownership of the stream

session::session(tcp::socket&& socket, const std::shared_ptr<registerd_events_t>& registerd_events)
    : stream_(std::move(socket)),
    registerd_events(registerd_events)
{}
// Start the asynchronous operation
void session::run()
{
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(
        stream_.get_executor(),
        beast::bind_front_handler(&session::do_read, shared_from_this())
    );
}

void session::do_read()
{
    // Make the request empty before reading,
    // otherwise the operation behavior is undefined.
    req_ = {};

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(50));
    std::cout << "log: do_read()" << std::endl;
    // Read a request
    http::async_read(
        stream_, buffer_, req_,
        beast::bind_front_handler(&session::on_read, shared_from_this())
    );
}

void session::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    std::cout << "log: on_read()" << std::endl;
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream) {
        return do_close();
    }
    if (ec) {
        return fail(ec, "read");
    }
    // Send the response
    this->handle_request();
}

void session::handle_request()
{
    const auto method = std::size_t(this->req_.method());
    std::uint8_t unknown_method_cnt = 0;
    if (method < this->registerd_events->strict_match.size()) {
        auto& events = this->registerd_events->strict_match[method];
        if (events.empty()) {
            ++unknown_method_cnt;
        }
        else {
            auto it = events.find(this->req_.target().to_string());
            if (it != events.end()) {
                session_send send(*this);
                it->second(send, std::move(this->req_));
                return;
            }
        }
    }
    if (method < this->registerd_events->regex_match.size()) {
        auto& events = this->registerd_events->regex_match[method];
        if (events.empty()) {
            ++unknown_method_cnt;
        }
        else {
            auto target = this->req_.target().to_string();
            for (auto&& e : events) {
                if (std::smatch m; std::regex_match(target, m, e.first)) {
                    session_send send(*this);
                    e.second(send, std::move(this->req_), m);
                    return;
                }
            }
        }
    }
    if (2u == unknown_method_cnt) {
        http::response<http::string_body> res{ http::status::bad_request, this->req_.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(this->req_.keep_alive());
        res.body() = "Unknown HTTP-method";
        res.prepare_payload();
        send(*this, std::move(res));
    }
}

void session::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
{
    std::cout << "log: on_write()" << std::endl;
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        return fail(ec, "write");
    }
    if (close) {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        return do_close();
    }

    // We're done with the response so delete it
    res_ = nullptr;

    // Read another request
    do_read();
}

void session::do_close()
{
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}
}
listener::listener(io_context& ioc, tcp::endpoint endpoint)
    : ioc_(ioc), acceptor_(net::make_strand(ioc)), is_running{},
    registerd_events(std::make_shared<detail::registerd_events_t>())
{
    registerd_events->strict_match.reserve(http_verb_max);
    registerd_events->regex_match.reserve(http_verb_max);
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fail(ec, "open");
        return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option");
        return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        fail(ec, "listen");
        return;
    }
}

std::shared_ptr<listener> listener::create(io_context& ioc, tcp::endpoint endpoint)
{
    // ref: https://gintenlabo.hatenablog.com/entry/20131211/1386771626
    struct helper : listener { helper(io_context& ioc, tcp::endpoint endpoint) : listener(ioc, endpoint) {} };
    return std::make_shared<helper>(ioc, endpoint);
}

// Start accepting incoming connections

void listener::run()
{
    do_accept();
}

void listener::canRegister() const
{
    if (this->is_running) {
        throw std::logic_error("HttpServer is running.");
    }
}

void listener::do_accept()
{
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&listener::on_accept, shared_from_this())
    );
}

void listener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec) {
        fail(ec, "accept");
    }
    else {
        // Create the session and run it
        std::make_shared<detail::session>(std::move(socket), registerd_events)->run();
    }

    // Accept another connection
    do_accept();
}
}

//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//   This file was modified from its original

#include "server.hh"
#include "connection.hh"
#include "ssl_server.hh"
#include "openssl-init.hh"
#include "plain_server.hh"

#include <utility>
#include <openssl/ssl.h>

namespace http {

namespace server {

static bool exists(const std::string& path)
{
    struct stat s;
    return stat(path.c_str(), &s) == 0;
}

server::server(const boost::program_options::variables_map* config,
               httpserver::routes* routes)
    : io_service_()
    , connection_manager_()
    , request_handler_(routes, *config)
{
    // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
    boost::asio::ip::tcp::resolver resolver(io_service_);
    boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve({
        (*config)["ipaddress"].as<std::string>(),
        (*config)["port"].as<std::string>()
    });

    tcp::acceptor tcp_acceptor(io_service_);
    tcp_acceptor.open(endpoint.protocol());
    tcp_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    tcp_acceptor.bind(endpoint);
    tcp_acceptor.listen();

    if (config->count("ssl")) {
        ensure_openssl_initialized();

        auto ca_cert_path = (*config)["cacert"].as<std::string>();
        auto cert_path = (*config)["cert"].as<std::string>();
        auto key_path = (*config)["key"].as<std::string>();

        bool valid = true;
        for (auto& path : {ca_cert_path, cert_path, key_path}) {
            if (!exists(path)) {
                std::cerr << "Not found: " << path << "\n";
                valid = false;
            }
        }

        if (!valid) {
            std::cerr << "Please visit https://github.com/cloudius-systems/osv/wiki/The-RESTful-API#configuring-ssl\n";
            throw std::runtime_error("invalid configuration");
        }

        ssl::context ctx = make_ssl_context(ca_cert_path, cert_path, key_path);
        acceptor_.reset(new ssl_acceptor(io_service_, std::move(ctx), std::move(tcp_acceptor)));
    } else {
        acceptor_.reset(new plain_acceptor(io_service_, std::move(tcp_acceptor)));
    }

    acceptor_->do_accept(std::bind(&server::on_connected, this, std::placeholders::_1));
}

void server::on_connected(std::shared_ptr<transport> t)
{
    connection_manager_.start(std::make_shared<connection>(
        t, connection_manager_, request_handler_));
}

void server::close()
{
    io_service_.dispatch([&] {
        acceptor_->close();
        connection_manager_.stop_all();
        io_service_.stop();
    });
}

void server::run()
{
    // The io_service::run() call will block until all asynchronous operations
    // have finished. While the server is running, there is always at least one
    // asynchronous operation outstanding: the asynchronous accept call waiting
    // for new incoming connections.
    io_service_.run();
}

} // namespace server

} // namespace http

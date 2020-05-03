#include <boost_beast_server/http_server.hpp>
#include <iostream>
namespace http = boost::beast::http;           // from <boost/beast/http.hpp>
int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc != 3)
    {
        std::cerr <<
            "Usage: http-server-async <address> <port>\n" <<
            "Example:\n" <<
            "    http-server-async 0.0.0.0 8080" << std::endl;
        return EXIT_FAILURE;
    }
    auto const address = server::net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::stoi(argv[2]));
    server::tcp::endpoint endpoint{ address, port };
    std::cout << "http://" << endpoint << std::endl;
    const auto threads = std::max(1u, std::thread::hardware_concurrency());

    // The io_context is required for all I/O
    server::io_context ioc{ int(threads) };
    // Create and launch a listening port
    auto listener = server::listener::create(ioc, endpoint);
    listener->get("/", [](server::request&& req) {
        return server::create_text_responce(req, http::status::ok, "text/plain", "arikitari");
    });
    listener->run();

    // Run the I/O service on the requested number of threads
    server::run_io_service(ioc, threads);

    return EXIT_SUCCESS;
}

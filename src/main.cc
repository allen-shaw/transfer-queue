#include "transfer_queue/echo_server.h"

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/log.hh>
#include <fmt/core.h>

namespace bpo = boost::program_options;

// Declared in echo_client.cc
seastar::future<> run_echo_client(const std::string& server_addr, uint16_t port,
                                   const std::string& message);

int main(int argc, char** argv) {
    seastar::app_template app;

    app.add_options()
        ("mode", bpo::value<std::string>()->default_value("server"), "Run mode: server or client")
        ("port", bpo::value<uint16_t>()->default_value(10000), "RPC port")
        ("server", bpo::value<std::string>()->default_value("127.0.0.1"), "Server address (client mode)")
        ("message", bpo::value<std::string>()->default_value("Hello Seastar!"), "Echo message (client mode)");

    return app.run(argc, argv, [&app] () -> seastar::future<> {
        auto& config = app.configuration();
        auto mode = config["mode"].as<std::string>();
        auto port = config["port"].as<uint16_t>();

        if (mode == "server") {
            // Server mode — start the echo service and keep running
            static echo_service service;
            co_await service.start(port);

            fmt::print("══════════════════════════════\n");
            fmt::print("  Echo Server running\n");
            fmt::print("  Port:   {}\n", port);
            fmt::print("  Shards: {}\n", seastar::smp::count);
            fmt::print("══════════════════════════════\n");

            // Keep running until the process is terminated
            co_await seastar::sleep(std::chrono::hours(24 * 365));

        } else if (mode == "client") {
            auto server_addr = config["server"].as<std::string>();
            auto message = config["message"].as<std::string>();

            co_await run_echo_client(server_addr, port, message);
        } else {
            fmt::print("Unknown mode: {}. Use --mode server or --mode client\n", mode);
        }
    });
}

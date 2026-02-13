#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/future.hh>
#include <iostream>

int main(int argc, char** argv) {
    seastar::app_template app;

    app.run(argc, argv, [] {
        std::cout << "Hello from Seastar on transfer-queue!" << std::endl;
        return seastar::make_ready_future<>();
    });
}

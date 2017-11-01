#include <chrono>
#include <iostream>
#include <memory>
#include <uv.h>

#include <cxxopts.hpp>
#include <fstream>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>
#include <sys/signalfd.h>

#include "network/blocking/ServerImpl.h"
#include "network/nonblocking/ServerImpl.h"
#include "network/uv/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

void sig_handler(int signo) {
    if (signo == SIGINT)
        printf("received SIGINT\n");
}

int main(int argc, char **argv) {
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        app_string << "-" << Afina::Version_SHA;
    }

    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("p,pid", "Print PID to file specified by filename", cxxopts::value<std::string>());
        options.add_options()("d,daemon", "Run application as daemon");
        options.add_options()("h,help", "Print usage info");
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    // Start boot sequence
    Application app;
    std::cout << "Starting " << app_string.str() << std::endl;

    // Build new storage instance
    std::string storage_type = "map_global";
    if (options.count("storage") > 0) {
        storage_type = options["storage"].as<std::string>();
    }

    if (storage_type == "map_global") {
        app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();
    } else {
        throw std::runtime_error("Unknown storage type");
    }

    // Build  & start network layer
    std::string network_type = "uv";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "uv") {
        app.server = std::make_shared<Afina::Network::UV::ServerImpl>(app.storage);
    } else if (network_type == "blocking") {
        app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Run daemon
    if (options.count("daemon") > 0) {
        std::cout << "Disowning process.\n";
        auto f_ret = fork();
        if (f_ret > 0)
            return 0;
        else if (f_ret < 0) {
            std::cout << "Something went wrong. Can't start as daemon. Exiting.\n";
            return 0;
        }
        // here can be only child process
        if (::setsid() < 0) {
            std::cout << "Something went wrong. Can't start as daemon. Exiting.\n";
            return 0;
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Print PID
    do {
        std::string pid_filename;
        if (options.count("pid") > 0) {
            pid_filename = options["pid"].as<std::string>();
        } else {
            break;
        }

        std::ofstream pid_file;
        pid_file.open(pid_filename);
        if (!pid_file.is_open()) {
            std::cout << "Can't open file \"" << pid_filename << "\". Skipping -p flag.\n";
            break;
        }
        pid_file << ::getpid() << "\n";
        pid_file.close();
    } while (0);

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create");
        abort();
    }

    if (signal(SIGINT, sig_handler) == SIG_ERR)
        printf("\ncan't catch SIGINT\n");
    if (signal(SIGTERM, sig_handler) == SIG_ERR)
        printf("\ncan't catch SIGTERM\n");


    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    int signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);

    epoll_event event;
    event.data.fd = signal_fd;
    event.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, signal_fd, &event);
    int size = 10;
    int infinity = -1;
    epoll_event events[size];

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080);

        // Freeze current thread and process events
        std::cout << "Application started" << std::endl;
        //        uv_run(&loop, UV_RUN_DEFAULT);

        // and wait for events
        int r = epoll_wait(epollfd, events, size, infinity);
        if (r == -1) {
            close(epollfd);
            close(signal_fd);
            return 1;
        }

        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}

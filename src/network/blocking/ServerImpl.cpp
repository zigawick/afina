#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <afina/execute/Command.h>
#include <algorithm>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <protocol/Parser.h>
#include <unistd.h>

#include <afina/Storage.h>

#define buf_len 1234

namespace Afina {
namespace Network {
namespace Blocking {

void *ServerImpl::RunAcceptorProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        srv->RunAcceptor();
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

void *ServerImpl::RunConnectionProxy(void *p) {
    WorkerStruct *strct = reinterpret_cast<WorkerStruct *>(p);

    try {
        strct->parrent->RunConnection(strct->listen_socket, strct->offset);
    } catch (std::runtime_error &ex) {
        std::cerr << "Worker fails: " << ex.what() << std::endl;
    }
    return 0;
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunAcceptorProxy, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);
    for (int i = 0; i < connections.size(); i++)
    {
        if (finished[i].load())
            pthread_join (connections[i], 0);
    }
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makesthe OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);

    for (int i = 0; i < max_workers; i++) {
        finished.emplace_back(true);
    }
    connection_sockets.reserve(max_workers);
    connections.resize(max_workers);

    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives


        int iResult;
           struct timeval tv;
           fd_set rfds;
           FD_ZERO(&rfds);
           FD_SET(server_socket, &rfds);

           tv.tv_sec = 5;
           tv.tv_usec = 0;

           iResult = select(server_socket, &rfds, (fd_set *) 0, (fd_set *) 0, &tv);
           if(iResult <= 0)
           {
              break;
           }
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
        }

        {
            auto first_free = std::find_if(finished.begin(), finished.end(),
                                           [](const std::atomic_bool &elem) { return elem.load(); });
            if (first_free == finished.end()) {
                std::string msg = "Sorry all workers are busy\n";
                if (send(client_socket, msg.data(), msg.size(), 0) <= 0) {
                    close(client_socket);
                    close(server_socket);
                    throw std::runtime_error("Socket send() failed");
                }
                close(client_socket);
            } else {
                int free_offset = first_free - finished.begin();
                connection_sockets[free_offset] = WorkerStruct(this, client_socket, free_offset);
                finished[free_offset].store(false);
                if (pthread_create(&connections[free_offset], NULL, ServerImpl::RunConnectionProxy,
                                   &connection_sockets[free_offset]) < 0) {
                    throw std::runtime_error("Could not create worker thread");
                }
            }
        }
    }

    // Cleanup on exit...
    close(server_socket);
}

// See Server.h
void ServerImpl::RunConnection(int sock, int offset) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    char rcv_buf[buf_len];
    std::string buf;

    Protocol::Parser pr;
    while (running.load()) {
        sleep (7);
        size_t parsed = 0;
        pr.Reset();
        bool was_parsed = false;
        while (!was_parsed) {

            int res = recv(sock, rcv_buf, buf_len, 0);
            if (res < 0) {
                close(sock);
                finished[offset].store(true);
                return;
            }
            if (res == 0) {
                close(sock);
                finished[offset].store(true);
                return;
            }
            buf += std::string(rcv_buf, res);
            was_parsed = pr.Parse(buf, parsed);
            buf = buf.substr(parsed, buf.size() - parsed);
        }

        uint32_t body_size = 0;
        std::unique_ptr<Execute::Command> command = pr.Build(body_size);

        while (buf.size() < body_size) {
            int res = recv(sock, rcv_buf, buf_len, 0);
            if (res < 0) {
                close(sock);
                finished[offset].store(true);
                return;
            }
            buf += std::string(rcv_buf, res);
        }

        std::string out;
        try {
            command->Execute(*pStorage, buf.substr(0, body_size), out);
            if (send(sock, out.data(), out.size(), 0) <= 0) {
                close(sock);
                finished[offset].store(true);
                return;
            }
        } catch (std::runtime_error &ex) {
            std::string error = std::string("SERVER_ERROR ") + ex.what() + "\n";
            if (send(sock, error.data(), error.size(), 0) <= 0) {
                close(sock);
                finished[offset].store(true);
                return;
            }
        }

        buf = buf.substr(body_size, buf.size() - body_size);
    }
    close(sock);
    finished[offset].store(true);
}

} // namespace Blocking
} // namespace Network
} // namespace Afina

#ifndef AFINA_NETWORK_BLOCKING_SERVER_H
#define AFINA_NETWORK_BLOCKING_SERVER_H

#include <atomic>
#include <pthread.h>
#include <vector>
#include <deque>

#include <afina/network/Server.h>

namespace Afina {
namespace Network {
namespace Blocking {
class ServerImpl;

struct WorkerStruct {
    WorkerStruct(ServerImpl *p, int s, int o) : parrent(p), listen_socket(s), offset(o) {}
    ServerImpl *parrent = nullptr;
    int listen_socket = -1;
    int offset = -1;
};

/**
 * # Network resource manager implementation
 * Server that is spawning a separate thread for each connection
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps);
    ~ServerImpl();

    // See Server.h
    void Start(uint32_t port, uint16_t workers) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    /**
     * Method is running in the connection acceptor thread
     */
    void RunAcceptor();

    /**
     * Methos is running for each connection
     */
    void RunConnection(int sock, int offset);

private:
    static void *RunAcceptorProxy(void *p);
    static void *RunConnectionProxy(void *p);

    // Atomic flag to notify threads when it is time to stop. Note that
    // flag must be atomic in order to safely publisj changes cross thread
    // bounds
    std::atomic<bool> running;

    // Thread that is accepting new connections
    pthread_t accept_thread;

    // Maximum number of client allowed to exists concurrently
    // on server, permits access only from inside of accept_thread.
    // Read-only
    uint16_t max_workers;

    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint32_t listen_port;

    // Threads that are processing connection data, permits
    // access only from inside of accept_thread
    std::vector<pthread_t> connections;

    // client_socket for every connection, permits
    // access only from inside of accept_thread
    std::vector<WorkerStruct> connection_sockets;

    std::deque<std::atomic_bool> finished;
    int accept_socket = -1;
};

} // namespace Blocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_BLOCKING_SERVER_H

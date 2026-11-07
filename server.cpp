// g++ -O2 -std=c++17 server.cpp -o server -pthread
// ./server <port> <output_file>

#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cerrno>

#define BUF_SIZE 65536

std::atomic<uint64_t> total_received{ 0 };
std::atomic<uint64_t> total_file_size{ 0 };
std::atomic<bool> server_running{ true };
auto start_time = std::chrono::high_resolution_clock::now();
std::string timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t_c = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t_c), "%H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void handle_client(int connfd, int fd) {
    uint64_t chunk_size = 0, start_offset = 0;

    // === Receive header: [chunk_size, start_offset] ===
    if (recv(connfd, &chunk_size, sizeof(chunk_size), MSG_WAITALL) != sizeof(chunk_size) ||
        recv(connfd, &start_offset, sizeof(start_offset), MSG_WAITALL) != sizeof(start_offset)) {
        // std::cerr << "[" << timestamp() << "] [SERVER] Failed to receive header\n";
        close(connfd);
        return;
    }

    // === DONE marker check ===
    if (chunk_size == 0 && start_offset == 0) {
        uint64_t ack_done = 0;
        send(connfd, &ack_done, sizeof(ack_done), 0);
        // std::cout << "[" << timestamp() << "] [SERVER] DONE marker received, shutting down\n";
        server_running = false;
        close(connfd);
        return;
    }

    // === Normal file transfer ===
  //  std::cout << "[" << timestamp() << "] [SERVER] Receiving subchunk offset "
     //   << start_offset << ", size " << chunk_size << "\n";

    char* buffer = new char[BUF_SIZE];
    uint64_t received = 0;
    while (received < chunk_size) {
        ssize_t n = recv(connfd, buffer,
            std::min<uint64_t>(BUF_SIZE, chunk_size - received), 0);
        if (n <= 0) break;

        // Write at the correct file offset
        ssize_t written = pwrite(fd, buffer, n, start_offset + received);
        if (written < 0) { perror("pwrite"); break; }

        received += n;
        total_received += n;
    }

    //  std::cout << "[" << timestamp() << "] [SERVER] Finished subchunk offset "
      //    << start_offset << ", received " << received << " bytes\n";
    struct stat st {};
    uint64_t written_size = 0;
    if (fstat(fd, &st) == 0) {
        written_size = st.st_size;
    }
    uint64_t total = total_file_size.load();
    if (written_size >= total) {
        std::cout << "[" << timestamp()
            << "] [SERVER] File complete on disk: "
            << written_size << " bytes\n";

        auto end_time = std::chrono::high_resolution_clock::now();
        double seconds =
            std::chrono::duration<double>(end_time - start_time).count();
        double mbps = (written_size * 8.0) / (seconds * 1e6);

        std::cout << "\n[" << timestamp() << "] [SERVER] Total received (counter): "
            << written_size << " bytes in " << seconds
            << " sec. Throughput: " << mbps << " Mbps\n";
        return;
    }
    delete[] buffer;
    close(connfd);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./server_mt_debug <port> <output_file>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string outpath = argv[2];

    // === Open output file ===
    int fd = open(outpath.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }

    // === Setup listening socket ===
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listenfd, 128) < 0) { perror("listen"); return 1; }

    std::cout << "[" << timestamp() << "] [SERVER] Listening on port " << port << "...\n";

    // === First connection: total file size ===

    {
        sockaddr_in cli{};
        socklen_t clilen = sizeof(cli);
        int temp_fd = accept(listenfd, (sockaddr*)&cli, &clilen);
        if (temp_fd < 0) { perror("accept"); return 1; }
        if (recv(temp_fd, &total_file_size, sizeof(total_file_size), MSG_WAITALL) != sizeof(total_file_size)) {
            perror("recv total_file_size");
            close(temp_fd);
            return 1;
        }
        close(temp_fd);
        std::cout << "[" << timestamp() << "] [SERVER] Expecting file size "
            << total_file_size << " bytes\n";
    }
    //struct stat st {};
    //uint64_t written_size = 0;
    //if (fstat(fd, &st) == 0) {
    //    written_size = st.st_size;
    //}
    //uint64_t total = total_file_size.load();
    //if (written_size >= total) {
    //    std::cout << "[" << timestamp()
    //        << "] [SERVER] File complete on disk 2 : "
    //        << written_size << " bytes\n";
    //    
    //     auto end_time = std::chrono::high_resolution_clock::now();
    //     double seconds =
    //         std::chrono::duration<double>(end_time - start_time).count();
    //     double mbps = (total_received * 8.0) / (seconds * 1e6);

    //     std::cout << "\n[" << timestamp() << "] [SERVER] Total received (counter): "
    //         << total_received << " bytes in " << seconds
    //         << " sec. Throughput: " << mbps << " Mbps\n";
    //         //break;
    //     close(listenfd);
    //     close(fd);
    //     return 0;
    //}
    // === Accept multiple subchunk connections ===

    std::vector<std::thread> threads;

    while (server_running) {
        sockaddr_in cli{};
        socklen_t clilen = sizeof(cli);
        int connfd = accept(listenfd, (sockaddr*)&cli, &clilen);
        if (!server_running) {
            if (connfd >= 0) close(connfd);
            break;
        }
        if (connfd < 0) {
            if (!server_running) break;
            if (errno == EBADF) break;
            perror("accept");
            continue;
        }
        // spawn thread to handle this client
        threads.emplace_back(handle_client, connfd, fd);
    }

    for (auto& t : threads) t.join();
    close(listenfd);
    close(fd);


    return 0;
}

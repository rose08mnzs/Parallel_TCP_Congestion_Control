// g++ -O2 -std=c++17 client.cpp -o client -pthread
// ./client <server_ip> <port> <file_path> <threads>
//64 threads is best performance in 20% packet drop 

#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>

#define BUF_SIZE 65536

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

void send_subchunk(const std::string& server_ip, int port,
    const std::string& file_path,
    uint64_t sub_offset, uint64_t sub_size) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return; }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); close(sockfd); return;
    }

    // === Send header: [chunk_size, start_offset] ===
    send(sockfd, &sub_size, sizeof(sub_size), 0);
    send(sockfd, &sub_offset, sizeof(sub_offset), 0);

    std::ifstream in(file_path, std::ios::binary);
    in.seekg(sub_offset);

    // std::cout << "[" << timestamp() << "] [CLIENT] Sending subchunk offset "
      //   << sub_offset << ", size " << sub_size << "\n";

    char* buffer = new char[BUF_SIZE];
    uint64_t sent = 0;
    while (sent < sub_size) {
        uint64_t to_read = std::min<uint64_t>(BUF_SIZE, sub_size - sent);
        in.read(buffer, to_read);
        std::streamsize n = in.gcount();
        if (n <= 0) break;
        uint64_t sent_now = 0;
        while (sent_now < (uint64_t)n) {
            ssize_t m = send(sockfd, buffer + sent_now, n - sent_now, 0);
            if (m <= 0) break;
            sent_now += m;
            sent += m;
        }
    }

    // std::cout << "[" << timestamp() << "] [CLIENT] Finished subchunk offset "
      //   << sub_offset << ", sent " << sent << " bytes\n";

    delete[] buffer;
    close(sockfd);
}

void send_done_marker(const std::string& server_ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return;

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd); return;
    }

    // === Send DONE marker (0,0) ===
    uint64_t done = 0;
    send(sockfd, &done, sizeof(done), 0);
    send(sockfd, &done, sizeof(done), 0);

    uint64_t ack;
    recv(sockfd, &ack, sizeof(ack), MSG_WAITALL);
    std::cout << "[" << timestamp() << "] [CLIENT] DONE marker sent, ACK received\n";

    close(sockfd);
}

int main(int argc, char* argv[]) {
    if (argc < 4 || argc > 5) {
        std::cerr << "Usage: ./client_mt_debug <server_ip> <port> <file_path> [threads]\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int port = std::stoi(argv[2]);
    std::string file_path = argv[3];
    int num_threads = (argc == 5) ? std::stoi(argv[4]) : 4;

    // === Check file ===
    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in) { std::cerr << "File not found\n"; return 1; }
    uint64_t filesize = in.tellg();
    in.close();

    // === Send total file size ===
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);
        connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr));
        send(sockfd, &filesize, sizeof(filesize), 0);
        close(sockfd);
    }
    std::cout << "[" << timestamp() << "] [CLIENT] Total file size: "
        << filesize << " bytes\n";

    // === Split file into chunks for threads ===
    uint64_t chunk_size = filesize / num_threads;
    uint64_t last_chunk = chunk_size + (filesize % num_threads);

    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; i++) {
        uint64_t offset = i * chunk_size;
        uint64_t this_size = (i == num_threads - 1) ? last_chunk : chunk_size;
        threads.emplace_back(send_subchunk, server_ip, port, file_path, offset, this_size);
    }

    for (auto& t : threads) t.join();
    send_done_marker(server_ip, port);

    auto end_time = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();
    double mbps = (filesize * 8.0) / (seconds * 1e6);
    // std::cout << "[" << timestamp() << "] [CLIENT] Sent " << filesize
      //   << " bytes in " << seconds << " sec. Throughput: " << mbps << " Mbps\n";

    return 0;
}

/**
 * @file server.cpp 
 * @brief Кластер-сервер для параллельного численного интегрирования
 * @author Воронин Дмитрий
 */

#ifdef _WIN32
    /**
     * @brief Windows-specific сокетные заголовки
     */
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define INVALID_SOCKET -1
#else
    /**
     * @brief POSIX сокетные заголовки (Linux/macOS)
     */
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <sstream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <chrono>

/**
 * @class CrossPlatformServer
 * @brief Кроссплатформенный многопоточный кластер-сервер
 * 
 * Распределяет задачи численного интегрирования между клиентами.
 * Поддерживает Windows/Linux/macOS. Thread-safe логирование.
 */
class CrossPlatformServer {
private:
    //! Глобальная сумма результатов
    double total_sum = 0.0;
    
    //! Мьютекс для total_sum
    std::mutex sum_mutex;
    
    //! Активные клиенты
    std::vector<int> clients;
    
    //! Параметры задачи
    double a_global, b_global, h_global;
    
    //! Количество ядер клиентов
    int total_cores = 0;
    
    //! Флаг активности
    bool task_active = false;
    
    //! Лог-файл
    std::ofstream log_file;
    
    //! Мьютекс логирования
    std::mutex log_mutex;

    /**
     * @brief Подынтегральная функция 1/ln(x)
     */
    double one_over_ln(double x) {
        return x > 1.0 ? 1.0 / std::log(x) : 0.0;
    }

#ifdef _WIN32
    /**
     * @brief Инициализация WinSock
     */
    void init_winsock() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            log("ERROR", "WSAStartup failed");
            exit(1);
        }
    }
    
    void cleanup_winsock() {
        WSACleanup();
    }
#endif

    /**
     * @brief Временная метка для логов
     */
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    /**
     * @brief Thread-safe логирование
     */
    void log(const std::string& level, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto timestamp = get_timestamp();
        std::string log_entry = "[" + level + "] " + timestamp + " | " + message;
        
        if (log_file.is_open()) {
            log_file << log_entry << std::endl;
            log_file.flush();
        }
        std::cout << log_entry << std::endl;
    }

    /**
     * @brief Обработчик клиента (в отдельном потоке)
     */
    void handle_client(int client_fd);

    /**
     * @brief Распределение задач по клиентам
     */
    void distribute_tasks();

public:
    CrossPlatformServer();
    ~CrossPlatformServer();
    
    /**
     * @brief Запуск сервера
     */
    void start(int port = 65432);
};

// Конструктор
CrossPlatformServer::CrossPlatformServer() {
#ifdef _WIN32
    init_winsock();
#endif
    log_file.open("server.log", std::ios::app);
    log("INFO", "Кластер-сервер инициализирован");
}

// Деструктор
CrossPlatformServer::~CrossPlatformServer() {
    log("INFO", "Сервер остановлен");
    if (log_file.is_open()) {
        log_file.close();
    }
#ifdef _WIN32
    cleanup_winsock();
#endif
}

/**
 * @brief Главный метод запуска сервера
 */
void CrossPlatformServer::start(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log("ERROR", "Не удалось создать сокет");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        log("ERROR", "Ошибка bind() на порт " + std::to_string(port));
        close(server_fd);
        return;
    }

    if (listen(server_fd, 10) < 0) {
        log("ERROR", "Ошибка listen()");
        close(server_fd);
        return;
    }

    log("INFO", "Сервер запущен на порту " + std::to_string(port));

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            log("INFO", "Клиент подключился: " + std::to_string(client_fd));
            clients.push_back(client_fd);
            std::thread(&CrossPlatformServer::handle_client, this, client_fd).detach();
        } else {
            log("WARN", "Ошибка accept()");
        }
    }
    
    close(server_fd);
}

/**
 * @brief Обработка команд клиента
 */
void CrossPlatformServer::handle_client(int client_fd) {
    char buffer[1024] = {0};
    
    while (recv(client_fd, buffer, sizeof(buffer) - 1, 0) > 0) {
        std::istringstream iss(buffer);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "CORES") {
            int cores;
            iss >> cores;
            {
                std::lock_guard<std::mutex> lock(sum_mutex);
                total_cores += cores;
                log("INFO", "Клиент " + std::to_string(client_fd) + 
                    ": " + std::to_string(cores) + 
                    " ядер. Итого: " + std::to_string(total_cores));
            }
            send(client_fd, "OK", 2, 0);
        }
        else if (cmd == "START_INTEGRAL") {
            iss >> a_global >> b_global >> h_global;
            {
                std::lock_guard<std::mutex> lock(sum_mutex);
                total_sum = 0.0;
                task_active = true;
            }
            log("TASK", "∫[" + std::to_string(a_global) + ";" + 
                std::to_string(b_global) + "] h=" + std::to_string(h_global));
            distribute_tasks();
        }
        else if (cmd == "RESULT") {
            double result;
            iss >> result;
            {
                std::lock_guard<std::mutex> lock(sum_mutex);
                total_sum += result;
                log("RESULT", std::to_string(client_fd) + 
                    ": " + std::to_string(result) + 
                    ". Итого: " + std::to_string(total_sum));
            }
        }
        memset(buffer, 0, sizeof(buffer));
    }
    
    log("INFO", "Клиент " + std::to_string(client_fd) + " отключился");
    close(client_fd);
}

/**
 * @brief Распределение задач
 */
void CrossPlatformServer::distribute_tasks() {
    std::lock_guard<std::mutex> lock(sum_mutex);
    if (!task_active || total_cores == 0 || clients.empty()) {
        log("WARN", "Невозможно распределить задачи");
        return;
    }
    
    double segment_size = (b_global - a_global) / total_cores;
    
    for (size_t i = 0; i < clients.size() && i < static_cast<size_t>(total_cores); ++i) {
        double start = a_global + i * segment_size;
        double end = start + segment_size;
        
        char task[256];
        snprintf(task, sizeof(task), "TASK %.6f %.6f %.6f\n", start, end, h_global);
        send(clients[i], task, strlen(task), 0);
        
        log("TASK", "Клиент " + std::to_string(clients[i]) + 
            ": [" + std::to_string(start) + ";" + std::to_string(end) + "]");
    }
}

/**
 * @brief Точка входа
 */
int main() {
    try {
        CrossPlatformServer server;
        server.start(65432);
    } catch (const std::exception& e) {
        std::cerr << "Критическая ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

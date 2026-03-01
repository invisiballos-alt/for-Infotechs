/**
 * @file client.cpp
 * @brief Кроссплатформенный кластер-клиент для численного интегрирования
 * @author Воронин Дмитрий
 * @version 2.0
 * @date 2026-03-02
 * @copyright MIT License
 */

/**
 * @brief Windows-specific сокетные заголовки и макросы
 * Автоматическая линковка ws2_32.lib и замена close() → closesocket()
 */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define SHUT_RDWR SD_BOTH
#else
    /**
     * @brief POSIX сокетные заголовки (Linux/macOS)
     * Требуются для socket(), connect(), send(), recv()
     */
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SHUT_RDWR SHUT_RDWR
    #define INVALID_SOCKET -1
#endif

#include <iostream>
#include <iomanip>
#include <cstring>
#include <thread>
#include <cmath>
#include <chrono>

/**
 * @brief Инициализация сокетов (Windows: WinSock, POSIX: заглушка)
 */
#ifdef _WIN32
    /**
     * @brief Инициализация WinSock2 (только Windows)
     */
    inline void init_sockets() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
    }
    /**
     * @brief Очистка WinSock2 (только Windows)
     */
    inline void cleanup_sockets() {
        WSACleanup();
    }
#else
    /**
     * @brief Заглушки для POSIX (Linux/macOS)
     */
    inline void init_sockets() {}
    inline void cleanup_sockets() {}
#endif

/**
 * @class CrossPlatformClient
 * @brief Кроссплатформенный клиент кластерной системы интегрирования
 * 
 * Подключается к серверу, отправляет информацию о CPU,
 * получает задачи интегрирования и отправляет результаты.
 * 
 * @note Автоматическая инициализация/очистка сокетов через RAII
 */
class CrossPlatformClient {
private:
    //! Дескриптор сокета подключения к серверу
    int sock_fd = INVALID_SOCKET;
    
    //! Количество ядер CPU для распределения нагрузки
    int num_cores;

    /**
     * @brief Подынтегральная функция f(x) = 1/ln(x)
     * @param x Аргумент (x > 1.0)
     * @return Значение функции 1/ln(x) или 0 при x ≤ 1
     */
    double one_over_ln(double x) {
        return x > 1.0 ? 1.0 / std::log(x) : 0.0;
    }
    
    /**
     * @brief Численное интегрирование методом прямоугольников
     * @param a Начало отрезка [a;b]
     * @param b Конец отрезка [a;b]  
     * @param h Шаг дискретизации
     * @return Значение ∫[a;b] 1/ln(x) dx
     */
    double compute_integral(double a, double b, double h) {
        double sum = 0.0;
        int steps = static_cast<int>((b - a) / h);
        for (int i = 0; i < steps; ++i) {
            double x = a + i * h;
            sum += one_over_ln(x) * h;
        }
        return sum;
    }

    /**
     * @brief Определение количества логических ядер CPU
     * @return Количество ядер или 4 (fallback)
     */
    int get_cpu_cores() {
        int cores = std::thread::hardware_concurrency();
        return cores > 0 ? static_cast<int>(cores) : 4;
    }

public:
    /**
     * @brief Конструктор — инициализация клиента
     * Автоматически определяет количество ядер и инициализирует сокеты
     */
    CrossPlatformClient() : num_cores(get_cpu_cores()) {
        init_sockets();
        std::cout << "Клиент [" << num_cores << " ядер] готов" << std::endl;
    }
    
    /**
     * @brief Деструктор — очистка ресурсов
     * Закрывает сокет и освобождает WinSock (Windows)
     */
    ~CrossPlatformClient() {
        if (sock_fd != INVALID_SOCKET) {
            close(sock_fd);
        }
        cleanup_sockets();
    }
    
    /**
     * @brief Подключение к серверу
     * @param host IP-адрес или hostname сервера
     * @param port TCP-порт сервера (1024-65535)
     * @return true при успешном подключении
     */
    bool connect_to_server(const std::string& host, int port) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == INVALID_SOCKET) {
            std::cerr << "Ошибка создания сокета" << std::endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = inet_addr(host.c_str());

        if (::connect(sock_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Ошибка подключения: " << host << ":" << port << std::endl;
            return false;
        }
        
        std::cout << "Подключён: " << host << ":" << port << std::endl;
        return true;
    }
    
    /**
     * @brief Отправка информации о CPU на сервер
     * @return true если сервер ответил "OK"
     */
    bool send_cores_info() {
        std::string msg = "CORES " + std::to_string(num_cores) + "\n";
        int sent = send(sock_fd, msg.c_str(), msg.length(), 0);
        
        char buffer[1024];
        int received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        buffer[received] = '\0';
        
        std::cout << "Сервер ответил: " << buffer << std::endl;
        return received > 0 && strncmp(buffer, "OK", 2) == 0;
    }
    
    /**
     * @brief Основной цикл обработки задач от сервера
     * Получает TASK, вычисляет интеграл, отправляет RESULT
     */
    void work_loop() {
        char buffer[1024];
        while (true) {
            int bytes = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                std::cout << "Сервер отключился" << std::endl;
                break;
            }
            
            buffer[bytes] = '\0';
            
            if (strncmp(buffer, "TASK", 4) == 0) {
                double a, b, h;
                if (sscanf(buffer, "TASK %lf %lf %lf", &a, &b, &h) == 3) {
                    std::cout << "Задача: [" << std::fixed << std::setprecision(4)
                              << a << ";" << b << "] h=" << h << std::endl;
                    
                    double result = compute_integral(a, b, h);
                    
                    char result_msg[256];
                    snprintf(result_msg, sizeof(result_msg), "RESULT %.10f\n", result);
                    send(sock_fd, result_msg, strlen(result_msg), 0);
                    
                    std::cout << "Результат: " << result << std::endl;
                }
            }
        }
    }
    
    /**
     * @brief Главный метод запуска клиента
     * @param host IP сервера (по умолчанию localhost)
     * @param port TCP-порт (по умолчанию 65432)
     * @return true при успешной работе
     */
    bool run(const std::string& host = "127.0.0.1", int port = 65432) {
        if (!connect_to_server(host, port)) return false;
        if (!send_cores_info()) return false;
        work_loop();
        return true;
    }
};

/**
 * @brief Точка входа программы
 * Запускает клиент с параметрами по умолчанию (localhost:65432)
 * @return 0 при успехе, 1 при ошибке
 */
int main() {
    CrossPlatformClient client;
    return client.run() ? 0 : 1;
}

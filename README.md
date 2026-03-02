# Ссылка на репозиторий: 
https://github.com/invisiballos-alt/for-Infotechs.git

# Описание:

Данный проект - клиент-серверная система, где сервер-кластер распределяет математическую задачу каждый клиент-ПК пропорционально количеству ядер CPU на каждом ПК, а затем синхронизирует части-ответы от клиентов и выводит на экран результат вычисления. Программа создана таким образом, чтобы оптимизировать вычисления, используя мощности подключенных ПК-клиентов.


# Структура проекта:

- CMakeLists.txt     # Универсальная сборка
- server.cpp         # Кластер-сервер (распределяет задачи)
- client.cpp         # Кластер-клиент (вычисляет интегралы)
- README.md          # Эта документация


# Сборка
__находясь в папке проекта__
mkdir build && cd build
cmake ..
make

Windows (MinGW-w64/MSYS2)
# Установка MSYS2: winget install MSYS2.MSYS2
# В MSYS2 UCRT64 терминале:
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake

mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j

Windows (Visual Studio)
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release

# Команды для сервера 

__пример команды:__ 
START_INTEGRAL 2 10 0.01 
              (а) (b) (h)

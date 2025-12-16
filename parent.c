#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <semaphore.h>

#define SHARED_FILE "shared_data.bin"
#define MAX_CMD 256
#define SHARED_SIZE 4096

typedef struct {
    char data[MAX_CMD];
    char response[128];
    int terminate;
    sem_t data_ready;      // Семафор: данные готовы для обработки
    sem_t processing_done; // Семафор: обработка завершена
} shared_data_t;

int main() {
    // Создаем или открываем файл для memory mapping
    int fd = open(SHARED_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        perror("open shared file error");
        return 1;
    }
    
    // Устанавливаем размер файла
    if (ftruncate(fd, SHARED_SIZE) == -1) {
        perror("ftruncate error");
        close(fd);
        return 1;
    }
    
    // Memory mapping
    //Создает виртуальное отображение файла в адресное пространство процесса,
    //чтобы можно было работать с файлом как с обычным массивом в памяти.
    shared_data_t *shared = mmap(NULL, SHARED_SIZE, PROT_READ | PROT_WRITE, 
                                MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap error");
        close(fd);
        return 1;
    }
    
    // Инициализация shared memory
    memset(shared, 0, sizeof(shared_data_t));
    shared->terminate = 0;
    
    // Инициализация семафоров в разделяемой памяти
    if (sem_init(&shared->data_ready, 1, 0) == -1) {
        perror("sem_init data_ready error");
        munmap(shared, SHARED_SIZE);
        close(fd);
        return 1;
    }
    
    if (sem_init(&shared->processing_done, 1, 1) == -1) {
        perror("sem_init processing_done error");
        sem_destroy(&shared->data_ready);
        munmap(shared, SHARED_SIZE);
        close(fd);
        return 1;
    }
    
    // Получаем имя файла для результатов
    char fileName[MAX_CMD];
    const char *prompt = "Enter file name for results: ";
    write(1, prompt, strlen(prompt));
    ssize_t len = read(0, fileName, sizeof(fileName)-1);
    if (len <= 0) {
        const char *msg = "Error reading file name\n";
        write(2, msg, strlen(msg));
        sem_destroy(&shared->data_ready);
        sem_destroy(&shared->processing_done);
        munmap(shared, SHARED_SIZE);
        close(fd);
        return 1;
    }
    fileName[len] = 0;
    char *newline = strchr(fileName, '\n');
    if (newline) *newline = 0;
    
    // Создаем дочерний процесс
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork error");
        sem_destroy(&shared->data_ready);
        sem_destroy(&shared->processing_done);
        munmap(shared, SHARED_SIZE);
        close(fd);
        return 1;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        close(fd); // Закрываем файловый дескриптор в дочернем процессе
        
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());
        
        // Запускаем дочернюю программу
        execl("./child", "./child", fileName, SHARED_FILE, pid_str, NULL);
        
        // Если execl вернул управление - ошибка
        perror("execl error");
        exit(1);
    } else {
        // Родительский процесс
        close(fd); // Закрываем файловый дескриптор
        
        printf("Parent PID: %d, Child PID: %d\n", getpid(), pid);
        
        char cmd[MAX_CMD];
        while (1) {
            // Ждем завершения предыдущей обработки
            if (sem_wait(&shared->processing_done) == -1) {
                perror("sem_wait processing_done error");
                break;
            }
            
            // Проверяем флаг завершения
            if (shared->terminate) {
                printf("Child process terminated due to division by zero.\n");
                sem_post(&shared->processing_done);
                break;
            }
            
            // Получаем данные от пользователя
            const char *prompt_msg = "Enter numbers (int int int ...) or 'quit' to exit: ";
            write(1, prompt_msg, strlen(prompt_msg));
            ssize_t rlen = read(0, cmd, sizeof(cmd)-1);
            if (rlen <= 0) {
                sem_post(&shared->processing_done);
                break;
            }
            
            cmd[rlen] = 0;
            
            // Удаляем символ новой строки
            char *nl = strchr(cmd, '\n');
            if (nl) *nl = 0;
            
            // Проверяем команду выхода
            if (strcmp(cmd, "quit") == 0) {
                sem_post(&shared->processing_done);
                break;
            }
            
            // Копируем данные в shared memory
            strncpy(shared->data, cmd, MAX_CMD - 1);
            shared->data[MAX_CMD - 1] = '\0';
            shared->response[0] = '\0';
            
            // Сигнализируем дочернему процессу, что данные готовы
            if (sem_post(&shared->data_ready) == -1) {
                perror("sem_post data_ready error");
                break;
            }
            
            // Ждем завершения обработки дочерним процессом
            if (sem_wait(&shared->processing_done) == -1) {
                perror("sem_wait processing_done error");
                break;
            }
            
            // Выводим ответ от дочернего процесса
            if (strlen(shared->response) > 0) {
                printf("Child response: %s", shared->response);
                
                // Проверяем на деление на ноль
                if (strstr(shared->response, "Division by zero") != NULL) {
                    printf("Terminating due to division by zero.\n");
                    sem_post(&shared->data_ready); // Разбудим child для завершения
                    break;
                }
            }
        }
        
        // Завершаем работу
        shared->terminate = 1;
        
        // Сигнализируем дочернему процессу, если он еще ждет
        sem_post(&shared->data_ready);
        
        // Ждем завершения дочернего процесса
        wait(NULL);
        
        // Уничтожаем семафоры
        sem_destroy(&shared->data_ready);
        sem_destroy(&shared->processing_done);
        
        // Освобождаем memory mapping
        munmap(shared, SHARED_SIZE);
        
        // Удаляем временный файл
        unlink(SHARED_FILE);
    }
    
    return 0;
}
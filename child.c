#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <semaphore.h>

#define SHARED_SIZE 4096
#define MAX_CMD 256

typedef struct {
    char data[MAX_CMD];
    char response[128];
    int terminate;
    sem_t data_ready;      // Семафор: данные готовы для обработки
    sem_t processing_done; // Семафор: обработка завершена
} shared_data_t;

void int_to_str(int num, char *buf) {
    int i = 0;
    if (num < 0) {
        buf[i++] = '-';
        num = -num;
    }
    if (num == 0) {
        buf[i++] = '0';
    } else {
        char tmp[12];
        int j = 0;
        while (num > 0) {
            tmp[j++] = '0' + (num % 10);
            num /= 10;
        }
        while (j > 0) {
            buf[i++] = tmp[--j];
        }
    }
    buf[i] = '\0';
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        const char *msg = "Usage: child <result_file> <shared_file> [pid]\n";
        write(2, msg, strlen(msg));
        return 1;
    }
    
    // Открываем файл для записи результатов
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd == -1) {
        perror("open result file error");
        return 1;
    }
    
    // Открываем shared file для memory mapping
    int shm_fd = open(argv[2], O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("open shared file error");
        close(fd);
        return 1;
    }
    
    // Memory mapping
    shared_data_t *shared = mmap(NULL, SHARED_SIZE, PROT_READ | PROT_WRITE, 
                                MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap error");
        close(fd);
        close(shm_fd);
        return 1;
    }
    
    close(shm_fd); // Файловый дескриптор больше не нужен
    
    printf("Child process started. PID: %d\n", getpid());
    
    // Основной цикл обработки
    while (1) {
        // Ждем сигнала о готовности данных
        if (sem_wait(&shared->data_ready) == -1) {
            perror("sem_wait data_ready error");
            break;
        }
        
        // Проверяем флаг завершения
        if (shared->terminate) {
            sem_post(&shared->processing_done);
            break;
        }
        
        // Обрабатываем данные
        char line[MAX_CMD];
        strncpy(line, shared->data, MAX_CMD - 1);
        line[MAX_CMD - 1] = '\0';
        
        int nums[64], count = 0;
        char *token = strtok(line, " \t\n");
        while (token && count < 64) {
            nums[count++] = strtol(token, NULL, 10);
            token = strtok(NULL, " \t\n");
        }
        
        if (count < 2) {
            snprintf(shared->response, sizeof(shared->response), 
                    "Need at least two numbers\n");
        } else {
            int result = nums[0];
            int zero = 0;
            
            for (int i = 1; i < count; ++i) {
                if (nums[i] == 0) {
                    snprintf(shared->response, sizeof(shared->response),
                            "Division by zero detected\n");
                    zero = 1;
                    break;
                }
                result /= nums[i];
            }
            
            if (!zero) {
                char buf[32];
                int_to_str(result, buf);
                
                // Записываем в файл
                write(fd, buf, strlen(buf));
                write(fd, "\n", 1);
                
                snprintf(shared->response, sizeof(shared->response),
                        "Result written: %s\n", buf);
            } else {
                // Устанавливаем флаг завершения при делении на ноль
                shared->terminate = 1;
            }
        }
        
        // Сигнализируем родительскому процессу, что обработка завершена
        if (sem_post(&shared->processing_done) == -1) {
            perror("sem_post processing_done error");
            break;
        }
    }
    
    // Закрываем файлы и освобождаем ресурсы
    close(fd);
    munmap(shared, SHARED_SIZE);
    
    printf("Child process terminated.\n");
    return 0;
}
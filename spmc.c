#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define BUFFER_SIZE 10

// 循环缓冲区结构体
typedef struct {
    int *buffer;  // 缓冲区数据
    int size;     // 缓冲区大小
    int write_idx;       // 生产者写入位置
    int read_idx;      // 消费者读取位置
    int count;    // 缓冲区中的数据数量
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t full;   // 缓冲区满条件变量
    pthread_cond_t empty;  // 缓冲区空条件变量
} BoundedBuffer;

BoundedBuffer g_buffer;

void *producer(void *arg) {
    int item = 1;
    while (1) {
        pthread_mutex_lock(&g_buffer.lock);
        
        // 等待缓冲区非满
        while (g_buffer.count == g_buffer.size) {
            pthread_cond_wait(&g_buffer.full, &g_buffer.lock);
        }
        
        // 写入数据到缓冲区
        g_buffer.buffer[g_buffer.write_idx] = item;
        g_buffer.write_idx = (g_buffer.write_idx + 1) % g_buffer.size;
        g_buffer.count++;
        
        printf("Producer produced item %d\n", item);
        
        // 唤醒一个消费者
        pthread_cond_signal(&g_buffer.empty);
        
        pthread_mutex_unlock(&g_buffer.lock);
        
        item++;
    }
    
    return NULL;
}

void *consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&g_buffer.lock);
        
        // 等待缓冲区非空
        while (g_buffer.count == 0) {
            pthread_cond_wait(&g_buffer.empty, &g_buffer.lock);
        }
        
        // 读取数据并消费
        int item = g_buffer.buffer[g_buffer.read_idx];
        g_buffer.read_idx = (g_buffer.read_idx + 1) % g_buffer.size;
        g_buffer.count--;
        
        printf("Consumer %ld consumed item %d\n", (long)arg, item);
        
        // 唤醒生产者
        pthread_cond_signal(&g_buffer.full);
        
        pthread_mutex_unlock(&g_buffer.lock);
    }
    
    return NULL;
}

int main() {
    // 初始化缓冲区
    g_buffer.buffer = (int *)malloc(sizeof(int) * BUFFER_SIZE);
    g_buffer.size = BUFFER_SIZE;
    g_buffer.write_idx = 0;
    g_buffer.read_idx = 0;
    g_buffer.count = 0;
    pthread_mutex_init(&g_buffer.lock, NULL);
    pthread_cond_init(&g_buffer.full, NULL);
    pthread_cond_init(&g_buffer.empty, NULL);
    
    // 创建生产者线程
    pthread_t producerThreadId;
    pthread_create(&producerThreadId, NULL, producer, NULL);
    
    // 创建多个消费者线程
    int numConsumers = 3;
    pthread_t consumerThreadIds[numConsumers];
    for (long i = 0; i < numConsumers; i++) {
        pthread_create(&consumerThreadIds[i], NULL, consumer, (void *)i);
    }
    
    // 等待生产者和消费者线程结束
    pthread_join(producerThreadId, NULL);
    for (int i = 0; i < numConsumers; i++) {
        pthread_join(consumerThreadIds[i], NULL);
    }
    
    // 销毁互斥锁和条件变量，释放缓冲区内存
    pthread_mutex_destroy(&g_buffer.lock);
    pthread_cond_destroy(&g_buffer.full);
    pthread_cond_destroy(&g_buffer.empty);
    free(g_buffer.buffer);
    
    return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define BUFFER_SIZE 10

// 循环缓冲区结构体
typedef struct {
    int *buffer;  // 缓冲区数据
    int size;     // 缓冲区大小
    int in;       // 生产者写入位置
    int out;      // 消费者读取位置
    int count;    // 缓冲区中的数据数量
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t full;   // 缓冲区满条件变量
    pthread_cond_t empty;  // 缓冲区空条件变量
} BoundedBuffer;

BoundedBuffer buffer;

void *producer(void *arg) {
    int item = 1;
    while (1) {
        pthread_mutex_lock(&buffer.lock);
        
        // 等待缓冲区非满
        while (buffer.count == buffer.size) {
            pthread_cond_wait(&buffer.full, &buffer.lock);
        }
        
        // 写入数据到缓冲区
        buffer.buffer[buffer.in] = item;
        buffer.in = (buffer.in + 1) % buffer.size;
        buffer.count++;
        
        printf("Producer produced item %d\n", item);
        
        // 唤醒一个消费者
        pthread_cond_signal(&buffer.empty);
        
        pthread_mutex_unlock(&buffer.lock);
        
        item++;
    }
    
    return NULL;
}

void *consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&buffer.lock);
        
        // 等待缓冲区非空
        while (buffer.count == 0) {
            pthread_cond_wait(&buffer.empty, &buffer.lock);
        }
        
        // 读取数据并消费
        int item = buffer.buffer[buffer.out];
        buffer.out = (buffer.out + 1) % buffer.size;
        buffer.count--;
        
        printf("Consumer %ld consumed item %d\n", (long)arg, item);
        
        // 唤醒生产者
        pthread_cond_signal(&buffer.full);
        
        pthread_mutex_unlock(&buffer.lock);
    }
    
    return NULL;
}

int main() {
    // 初始化缓冲区
    buffer.buffer = (int *)malloc(sizeof(int) * BUFFER_SIZE);
    buffer.size = BUFFER_SIZE;
    buffer.in = 0;
    buffer.out = 0;
    buffer.count = 0;
    pthread_mutex_init(&buffer.lock, NULL);
    pthread_cond_init(&buffer.full, NULL);
    pthread_cond_init(&buffer.empty, NULL);
    
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
    pthread_mutex_destroy(&buffer.lock);
    pthread_cond_destroy(&buffer.full);
    pthread_cond_destroy(&buffer.empty);
    free(buffer.buffer);
    
    return 0;
}


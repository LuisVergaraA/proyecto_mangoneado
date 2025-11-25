// robots.c
// Recibe lista de mangos y corre la simulación con R robots (pthread).
// Uso: ./robots <port> <R> <X(cm/s)> <Z(cm)> <W(cm)> <label_time_ms> <B_prob>
// Ej:  ./robots 9000 4 10 30 200 200 0.05

#include "platform.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#define RECV_BUF 4096

typedef struct {
    int id;
    double pos; // cm along band (absolute reference)
    int active;
} Robot;

static Mango *mangos = NULL;
static int mango_count = 0;
static pthread_mutex_t *mango_locks = NULL;
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

static Robot *robots = NULL;
static int R = 0;
static double X_speed = 0.0;
static double Z_side = 0.0;
static double W_len = 0.0;
static int label_time_ms = 100;
static double B_fail = 0.0;

static double sim_time = 0.0;
static double dt = 0.05; // seconds
static double box_pos = -1000.0; // start far negative

static int count_tagged(){
    int c=0;
    for(int i=0;i<mango_count;i++) if(mangos[i].claimed) c++;
    return c;
}

static int all_tagged(){
    for(int i=0;i<mango_count;i++) if(!mangos[i].claimed) return 0;
    return 1;
}

void *robot_thread(void *arg){
    Robot *r = (Robot*)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (r->id * 0x9e3779b9));
    while(!all_tagged()){
        // simulate random failure check
        double p = (double)rand_r(&seed)/RAND_MAX;
        if(p < B_fail){
            r->active = 0;
            pthread_mutex_lock(&print_lock);
            printf("[Robot %d] FALLA\n", r->id);
            pthread_mutex_unlock(&print_lock);
            // downtime random 100..1000 ms
            int down = 100 + (rand_r(&seed) % 900);
            sleep_ms(down);
            r->active = 1;
            pthread_mutex_lock(&print_lock);
            printf("[Robot %d] RECUPERADO\n", r->id);
            pthread_mutex_unlock(&print_lock);
        }
        if(!r->active){
            sleep_ms((int)(dt*1000));
            continue;
        }

        // iterate mangoes and try claim those that are within tolerance
        double tolerance = (Z_side/2.0) * 0.15; // 15% of half-side
        for(int i=0;i<mango_count;i++){
            if(mangos[i].claimed) continue;
            double mango_abs_x = box_pos + mangos[i].x;
            double diff = fabs(mango_abs_x - r->pos);
            if(diff <= tolerance){
                // try to lock mango i
                if(pthread_mutex_trylock(&mango_locks[i])==0){
                    if(!mangos[i].claimed){
                        mangos[i].claimed = 1;
                        pthread_mutex_unlock(&mango_locks[i]);
                        pthread_mutex_lock(&print_lock);
                        printf("[Robot %d] etiquetando mango %d (rel x=%.2f) t=%.2f s\n",
                               r->id, i, mangos[i].x, sim_time);
                        pthread_mutex_unlock(&print_lock);
                        sleep_ms(label_time_ms);
                    } else {
                        pthread_mutex_unlock(&mango_locks[i]);
                    }
                }
            }
        }
        sleep_ms((int)(dt*1000));
    }
    return NULL;
}

int accept_and_read(int port){
    init_sockets();
    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock == INVALID_SOCKET){
        perror("socket");
        return -1;
    }
    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);

    if(bind(listen_sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR){
#ifdef _WIN32
        fprintf(stderr, "bind error: %d\n", WSAGetLastError());
#else
        perror("bind");
#endif
        close_socket(listen_sock); cleanup_sockets(); return -1;
    }
    if(listen(listen_sock, 1) == SOCKET_ERROR){
        perror("listen");
        close_socket(listen_sock); cleanup_sockets(); return -1;
    }
    printf("Robots: escuchando en puerto %d...\n", port);
    socket_t client = accept(listen_sock, NULL, NULL);
    if(client == INVALID_SOCKET){
        perror("accept");
        close_socket(listen_sock); cleanup_sockets(); return -1;
    }
    printf("Conexion aceptada.\n");

    // read into buffer and parse lines
    char buf[RECV_BUF+1];
    int offset=0;
    int header_read = 0;
    int expected = 0;
    while(1){
        int r = recv(client, buf+offset, RECV_BUF-offset, 0);
        if(r <= 0) break;
        offset += r;
        buf[offset] = '\0';
        // try to parse lines
        char *line = NULL;
        char *saveptr = NULL;
        line = strtok_r(buf, "\n", &saveptr);
        while(line){
            if(!header_read){
                // header: N Z
                int N;
                double Ztemp;
                if(sscanf(line, "%d %lf", &N, &Ztemp) >= 1){
                    expected = N;
                    if(Ztemp > 0) Z_side = Ztemp;
                    mango_count = expected;
                    mangos = (Mango*)calloc(mango_count, sizeof(Mango));
                    mango_locks = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t)*mango_count);
                    for(int i=0;i<mango_count;i++) {
                        mangos[i].claimed = 0;
                        pthread_mutex_init(&mango_locks[i], NULL);
                    }
                    header_read = 1;
                    printf("Header: N=%d Z=%.2f\n", expected, Z_side);
                }
            } else {
                if(strncmp(line, "END", 3) == 0) {
                    // done
                    close_socket(client);
                    close_socket(listen_sock);
                    cleanup_sockets();
                    return 0;
                }
                double x,y;
                if(sscanf(line, "%lf %lf", &x, &y) == 2){
                    static int idx = 0;
                    if(idx < mango_count){
                        mangos[idx].x = x;
                        mangos[idx].y = y;
                        mangos[idx].claimed = 0;
                        idx++;
                    }
                    if(idx == mango_count){
                        // we can stop reading more (but still accept END)
                    }
                }
            }
            // prepare for next strtok - but careful: strtok_r requires original buffer to remain
            line = strtok_r(NULL, "\n", &saveptr);
        }
        // reset buffer for next recv
        offset = 0;
    }

    close_socket(client);
    close_socket(listen_sock);
    cleanup_sockets();
    return 0;
}

int main(int argc, char **argv){
    if(argc < 8){
        fprintf(stderr, "Uso: %s <port> <R> <X(cm/s)> <Z(cm)> <W(cm)> <label_time_ms> <B_prob>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    R = atoi(argv[2]);
    X_speed = atof(argv[3]);
    Z_side = atof(argv[4]);
    W_len = atof(argv[5]);
    label_time_ms = atoi(argv[6]);
    B_fail = atof(argv[7]);

    if(R <= 0 || X_speed <= 0 || Z_side <= 0 || W_len <= 0){
        fprintf(stderr,"Parámetros inválidos\n");
        return 1;
    }

    // accept connection and read mangos
    if(accept_and_read(port) != 0){
        fprintf(stderr,"Error recibiendo datos\n");
        return 1;
    }
    if(mango_count <= 0){
        fprintf(stderr,"No se recibieron mangos\n");
        return 1;
    }

    printf("Recibidos %d mangos. Iniciando simulación con R=%d\n", mango_count, R);

    // allocate robots
    robots = (Robot*)malloc(sizeof(Robot)*R);
    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t)*R);
    for(int i=0;i<R;i++){
        robots[i].id = i;
        robots[i].active = 1;
        if(R==1) robots[i].pos = 0.0;
        else robots[i].pos = -W_len/2.0 + ((double)i)*(W_len/(R-1));
    }

    // spawn threads
    for(int i=0;i<R;i++){
        if(pthread_create(&threads[i], NULL, robot_thread, &robots[i]) != 0){
            perror("pthread_create");
            return 1;
        }
    }

    // main simulation loop
    sim_time = 0.0;
    box_pos = -W_len; // start so box traverses robots area
    double time_limit = 120.0; // seconds safety cap

    while(sim_time < time_limit && !all_tagged()){
        sim_time += dt;
        box_pos += X_speed * dt;
        if(((int)(sim_time*10)) % 50 == 0){
            pthread_mutex_lock(&print_lock);
            printf("[Sim] t=%.2f s, box_pos=%.2f cm, etiquetados=%d/%d\n",
                   sim_time, box_pos, count_tagged(), mango_count);
            pthread_mutex_unlock(&print_lock);
        }
        sleep_ms((int)(dt*1000));
    }

    // wait threads
    for(int i=0;i<R;i++) pthread_join(threads[i], NULL);

    int done = count_tagged();
    printf("Simulación finalizada: %d de %d mangos etiquetados\n", done, mango_count);

    // cleanup
    for(int i=0;i<mango_count;i++) pthread_mutex_destroy(&mango_locks[i]);
    free(mango_locks);
    free(mangos);
    free(robots);
    free(threads);

    return 0;
}


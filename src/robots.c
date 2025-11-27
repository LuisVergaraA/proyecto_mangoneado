// robots.c (VERSIÓN CORREGIDA)
// Recibe lista de mangos y corre la simulación con R robots (pthread).
// CORRECCIÓN CRÍTICA: Cada robot solo etiqueta mangos en SU ZONA asignada
// Uso: ./robots <port> <R> <X(cm/s)> <Z(cm)> <W(cm)> <label_time_ms> <B_prob>

#include "platform.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#define RECV_BUF 4096
#define MAX_ROBOTS 50
#define MAX_MANGOS 1000

typedef struct {
    int id;
    double pos;           // posición del robot en la banda (cm)
    double zone_start;    // inicio de zona de trabajo (cm)
    double zone_end;      // fin de zona de trabajo (cm)
    int active;           // 1=operativo, 0=fallado
    int mangos_tagged;    // contador de mangos etiquetados
    double total_work_time; // tiempo acumulado etiquetando
} Robot;

typedef struct {
    int total_mangos;
    int tagged_mangos;
    int missed_mangos;
    double simulation_time;
    int robot_failures;
} Metrics;

static Mango *mangos = NULL;
static int mango_count = 0;
static pthread_mutex_t *mango_locks = NULL;
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static Robot *robots = NULL;
static int R = 0;
static double X_speed = 0.0;
static double Z_side = 0.0;
static double W_len = 0.0;
static int label_time_ms = 100;
static double B_fail = 0.0;

static double sim_time = 0.0;
static double dt = 0.05; // seconds
static double box_pos = -1000.0;
static int simulation_running = 1;

static Metrics metrics = {0};

// Signal handler para limpieza
void cleanup_handler(int sig) {
    printf("\n\n[CLEANUP] Señal recibida (%d), limpiando recursos...\n", sig);
    simulation_running = 0;
    
    if(mango_locks) {
        for(int i = 0; i < mango_count; i++) {
            pthread_mutex_destroy(&mango_locks[i]);
        }
        free(mango_locks);
    }
    pthread_mutex_destroy(&print_lock);
    pthread_mutex_destroy(&metrics_lock);
    
    free(mangos);
    free(robots);
    
    printf("[CLEANUP] Recursos liberados correctamente.\n");
    exit(sig == SIGINT ? 0 : 1);
}

static int count_tagged() {
    int c = 0;
    for(int i = 0; i < mango_count; i++) 
        if(mangos[i].claimed) c++;
    return c;
}

static int all_tagged() {
    for(int i = 0; i < mango_count; i++) 
        if(!mangos[i].claimed) return 0;
    return 1;
}

// CORRECCIÓN CRÍTICA: Validar si mango está en la zona del robot
int is_mango_in_zone(Robot *r, int mango_idx, double current_box_pos) {
    double mango_abs_x = current_box_pos + mangos[mango_idx].x;
    
    // Especificación: "puede colocar una etiqueta una vez que la caja está 
    // justo en frente del eje de rotación del brazo y hasta que la caja 
    // llegue al eje de rotación del siguiente robot"
    return (mango_abs_x >= r->zone_start && mango_abs_x < r->zone_end);
}

void *robot_thread(void *arg) {
    Robot *r = (Robot*)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (r->id * 0x9e3779b9));
    
    pthread_mutex_lock(&print_lock);
    printf("[Robot %d] Iniciado - Zona: [%.2f, %.2f) cm\n", 
           r->id, r->zone_start, r->zone_end);
    pthread_mutex_unlock(&print_lock);
    
    while(simulation_running && !all_tagged()) {
        // Simular fallas aleatorias con probabilidad B
        if(r->active && B_fail > 0) {
            double p = (double)rand_r(&seed) / RAND_MAX;
            if(p < B_fail * dt) {  // Probabilidad por unidad de tiempo
                r->active = 0;
                pthread_mutex_lock(&print_lock);
                printf("[Robot %d] ⚠ FALLA detectada (t=%.2fs)\n", r->id, sim_time);
                pthread_mutex_unlock(&print_lock);
                
                pthread_mutex_lock(&metrics_lock);
                metrics.robot_failures++;
                pthread_mutex_unlock(&metrics_lock);
                
                // Tiempo de recuperación aleatorio
                int downtime = 100 + (rand_r(&seed) % 900);
                sleep_ms(downtime);
                
                r->active = 1;
                pthread_mutex_lock(&print_lock);
                printf("[Robot %d] ✓ Recuperado (downtime=%dms)\n", r->id, downtime);
                pthread_mutex_unlock(&print_lock);
            }
        }
        
        if(!r->active) {
            sleep_ms((int)(dt * 1000));
            continue;
        }
        
        // Buscar mangos en la zona asignada
        for(int i = 0; i < mango_count; i++) {
            if(mangos[i].claimed) continue;
            
            // CORRECCIÓN: Verificar zona asignada
            if(is_mango_in_zone(r, i, box_pos)) {
                // Intentar adquirir lock del mango
                if(pthread_mutex_trylock(&mango_locks[i]) == 0) {
                    // Double-check después de adquirir lock
                    if(!mangos[i].claimed && 
                       is_mango_in_zone(r, i, box_pos)) {
                        
                        mangos[i].claimed = 1;
                        
                        pthread_mutex_lock(&print_lock);
                        printf("[Robot %d] 🏷️  Etiquetando mango %d "
                               "(pos_rel=%.2f, box_pos=%.2f, t=%.2fs)\n",
                               r->id, i, mangos[i].x, box_pos, sim_time);
                        pthread_mutex_unlock(&print_lock);
                        
                        pthread_mutex_unlock(&mango_locks[i]);
                        
                        // Simular tiempo de etiquetado
                        sleep_ms(label_time_ms);
                        
                        r->mangos_tagged++;
                        r->total_work_time += label_time_ms / 1000.0;
                        
                        // Validar si el mango se movió fuera de alcance
                        if(!is_mango_in_zone(r, i, box_pos)) {
                            pthread_mutex_lock(&print_lock);
                            printf("[Robot %d] ⚠ Mango %d salió de zona durante etiquetado\n",
                                   r->id, i);
                            pthread_mutex_unlock(&print_lock);
                        }
                    } else {
                        pthread_mutex_unlock(&mango_locks[i]);
                    }
                }
            }
        }
        
        sleep_ms((int)(dt * 1000));
    }
    
    pthread_mutex_lock(&print_lock);
    printf("[Robot %d] Finalizando - Etiquetó %d mangos (%.2fs trabajo)\n",
           r->id, r->mangos_tagged, r->total_work_time);
    pthread_mutex_unlock(&print_lock);
    
    return NULL;
}

int accept_and_read(int port) {
    init_sockets();
    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }
    
    // Permitir reuso de puerto
    int opt = 1;
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);
    
    if(bind(listen_sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
#ifdef _WIN32
        fprintf(stderr, "bind error: %d\n", WSAGetLastError());
#else
        perror("bind");
#endif
        close_socket(listen_sock);
        cleanup_sockets();
        return -1;
    }
    
    if(listen(listen_sock, 1) == SOCKET_ERROR) {
        perror("listen");
        close_socket(listen_sock);
        cleanup_sockets();
        return -1;
    }
    
    printf("Robots: Escuchando en puerto %d...\n", port);
    
    socket_t client = accept(listen_sock, NULL, NULL);
    if(client == INVALID_SOCKET) {
        perror("accept");
        close_socket(listen_sock);
        cleanup_sockets();
        return -1;
    }
    
    printf("Conexión aceptada desde vision.\n");
    
    // Leer datos
    char buf[RECV_BUF + 1];
    int offset = 0;
    int header_read = 0;
    int expected = 0;
    int received_count = 0;
    
    while(1) {
        int r = recv(client, buf + offset, RECV_BUF - offset, 0);
        if(r <= 0) break;
        
        offset += r;
        buf[offset] = '\0';
        
        char *line = NULL;
        char *saveptr = NULL;
        line = strtok_r(buf, "\n", &saveptr);
        
        while(line) {
            if(!header_read) {
                int N;
                double Ztemp;
                if(sscanf(line, "%d %lf", &N, &Ztemp) >= 1) {
                    if(N <= 0 || N > MAX_MANGOS) {
                        fprintf(stderr, "Error: N=%d fuera de rango [1,%d]\n", 
                                N, MAX_MANGOS);
                        close_socket(client);
                        close_socket(listen_sock);
                        cleanup_sockets();
                        return -1;
                    }
                    
                    expected = N;
                    if(Ztemp > 0) Z_side = Ztemp;
                    
                    mango_count = expected;
                    mangos = (Mango*)calloc(mango_count, sizeof(Mango));
                    mango_locks = (pthread_mutex_t*)malloc(
                        sizeof(pthread_mutex_t) * mango_count);
                    
                    for(int i = 0; i < mango_count; i++) {
                        mangos[i].claimed = 0;
                        pthread_mutex_init(&mango_locks[i], NULL);
                    }
                    
                    header_read = 1;
                    printf("Header recibido: N=%d, Z=%.2f cm\n", expected, Z_side);
                }
            } else {
                if(strncmp(line, "END", 3) == 0) {
                    close_socket(client);
                    close_socket(listen_sock);
                    cleanup_sockets();
                    return 0;
                }
                
                double x, y;
                if(sscanf(line, "%lf %lf", &x, &y) == 2) {
                    if(received_count < mango_count) {
                        mangos[received_count].x = x;
                        mangos[received_count].y = y;
                        mangos[received_count].claimed = 0;
                        received_count++;
                    }
                }
            }
            
            line = strtok_r(NULL, "\n", &saveptr);
        }
        
        offset = 0;
    }
    
    close_socket(client);
    close_socket(listen_sock);
    cleanup_sockets();
    return 0;
}

void print_final_statistics() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║           ESTADÍSTICAS FINALES                       ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("  Mangos totales:      %d\n", metrics.total_mangos);
    printf("  Mangos etiquetados:  %d (%.1f%%)\n", 
           metrics.tagged_mangos,
           100.0 * metrics.tagged_mangos / metrics.total_mangos);
    printf("  Mangos perdidos:     %d\n", metrics.missed_mangos);
    printf("  Tiempo simulación:   %.2f s\n", metrics.simulation_time);
    printf("  Fallas de robots:    %d\n", metrics.robot_failures);
    printf("\n  Rendimiento por robot:\n");
    for(int i = 0; i < R; i++) {
        double utilization = robots[i].total_work_time / metrics.simulation_time * 100.0;
        printf("    Robot %d: %d mangos, %.1f%% utilización\n",
               i, robots[i].mangos_tagged, utilization);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    // Registrar signal handlers
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    
    if(argc < 8) {
        fprintf(stderr, "Uso: %s <port> <R> <X(cm/s)> <Z(cm)> <W(cm)> "
                       "<label_time_ms> <B_prob>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 9000 4 10 30 200 200 0.05\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    R = atoi(argv[2]);
    X_speed = atof(argv[3]);
    Z_side = atof(argv[4]);
    W_len = atof(argv[5]);
    label_time_ms = atoi(argv[6]);
    B_fail = atof(argv[7]);
    
    // Validación de entrada
    if(port < 1024 || port > 65535) {
        fprintf(stderr, "Error: Puerto debe estar entre 1024-65535\n");
        return 1;
    }
    if(R <= 0 || R > MAX_ROBOTS) {
        fprintf(stderr, "Error: R debe estar entre 1-%d\n", MAX_ROBOTS);
        return 1;
    }
    if(X_speed <= 0 || Z_side <= 0 || W_len <= 0) {
        fprintf(stderr, "Error: X, Z, W deben ser > 0\n");
        return 1;
    }
    if(label_time_ms < 0) {
        fprintf(stderr, "Error: label_time_ms debe ser >= 0\n");
        return 1;
    }
    if(B_fail < 0.0 || B_fail > 1.0) {
        fprintf(stderr, "Error: B debe estar entre 0.0-1.0\n");
        return 1;
    }
    
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║        MangoNeado - Sistema de Etiquetado           ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("Parámetros:\n");
    printf("  Puerto:          %d\n", port);
    printf("  Robots (R):      %d\n", R);
    printf("  Velocidad (X):   %.2f cm/s\n", X_speed);
    printf("  Lado caja (Z):   %.2f cm\n", Z_side);
    printf("  Longitud (W):    %.2f cm\n", W_len);
    printf("  Tiempo etiqueta: %d ms\n", label_time_ms);
    printf("  Prob. falla (B): %.3f\n\n", B_fail);
    
    // Recibir datos de vision
    if(accept_and_read(port) != 0) {
        fprintf(stderr, "Error recibiendo datos de vision\n");
        return 1;
    }
    
    if(mango_count <= 0) {
        fprintf(stderr, "Error: No se recibieron mangos\n");
        return 1;
    }
    
    metrics.total_mangos = mango_count;
    
    printf("✓ Recibidos %d mangos. Iniciando simulación...\n\n", mango_count);
    
    // Inicializar robots con zonas asignadas
    robots = (Robot*)calloc(R, sizeof(Robot));
    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * R);
    
    for(int i = 0; i < R; i++) {
        robots[i].id = i;
        robots[i].active = 1;
        robots[i].mangos_tagged = 0;
        robots[i].total_work_time = 0.0;
        
        // Distribución homogénea según especificación
        if(R == 1) {
            robots[i].pos = 0.0;
            robots[i].zone_start = -W_len / 2.0;
            robots[i].zone_end = W_len / 2.0;
        } else {
            robots[i].pos = -W_len / 2.0 + ((double)i) * (W_len / (R - 1));
            robots[i].zone_start = robots[i].pos;
            
            if(i < R - 1) {
                robots[i].zone_end = -W_len / 2.0 + 
                                     ((double)(i + 1)) * (W_len / (R - 1));
            } else {
                robots[i].zone_end = W_len / 2.0;
            }
        }
    }
    
    // Crear threads de robots
    for(int i = 0; i < R; i++) {
        if(pthread_create(&threads[i], NULL, robot_thread, &robots[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    // Loop principal de simulación
    sim_time = 0.0;
    box_pos = -W_len - Z_side; // Empezar antes de la zona de robots
    double time_limit = 120.0;
    
    while(sim_time < time_limit && !all_tagged() && simulation_running) {
        sim_time += dt;
        box_pos += X_speed * dt;
        
        // Imprimir estado cada 5 segundos
        if(((int)(sim_time * 10)) % 50 == 0) {
            int tagged = count_tagged();
            printf("[Sim t=%.1fs] Box pos=%.1f cm | Etiquetados: %d/%d (%.1f%%)\n",
                   sim_time, box_pos, tagged, mango_count,
                   100.0 * tagged / mango_count);
        }
        
        sleep_ms((int)(dt * 1000));
    }
    
    simulation_running = 0;
    
    // Esperar threads
    for(int i = 0; i < R; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Recolectar métricas finales
    metrics.tagged_mangos = count_tagged();
    metrics.missed_mangos = mango_count - metrics.tagged_mangos;
    metrics.simulation_time = sim_time;
    
    print_final_statistics();
    
    // Limpieza
    for(int i = 0; i < mango_count; i++) {
        pthread_mutex_destroy(&mango_locks[i]);
    }
    pthread_mutex_destroy(&print_lock);
    pthread_mutex_destroy(&metrics_lock);
    
    free(mango_locks);
    free(mangos);
    free(robots);
    free(threads);
    
    return (metrics.tagged_mangos == metrics.total_mangos) ? 0 : 1;
}

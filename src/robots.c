// robots.c
// Mejoras implementadas:
// 1. Activación dinámica de robots según carga (Requisito 6)
// 2. Reasignación de zonas cuando robots fallan
// 3. Mejor manejo de redundancia

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
    double pos;
    double zone_start;
    double zone_end;
    int active;              // 1=operativo, 0=inactivo
    int should_work;         // 1=debe trabajar según carga, 0=standby
    int failed;              // 1=fallado temporalmente
    int mangos_tagged;
    double total_work_time;
    double idle_time;        // tiempo inactivo
} Robot;

typedef struct {
    int total_mangos;
    int tagged_mangos;
    int missed_mangos;
    double simulation_time;
    int robot_failures;
    int robots_active;       // robots actualmente activos
    int robots_needed;       // robots necesarios según carga
} Metrics;

static Mango *mangos = NULL;
static int mango_count = 0;
static pthread_mutex_t *mango_locks = NULL;
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t robot_state_lock = PTHREAD_MUTEX_INITIALIZER;

static Robot *robots = NULL;
static int R = 0;
static double X_speed = 0.0;
static double Z_side = 0.0;
static double W_len = 0.0;
static int label_time_ms = 100;
static double B_fail = 0.0;

static double sim_time = 0.0;
static double dt = 0.05;
static double box_pos = -1000.0;
static int simulation_running = 1;

static Metrics metrics = {0};

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
    pthread_mutex_destroy(&robot_state_lock);
    
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

// Calcular cuántos robots son necesarios según carga actual
int calculate_needed_robots(int N, double X, double Z, double W, int label_ms) {
    // Tiempo que tarda la caja en atravesar completamente la banda
    double total_time = (W + Z) / X;  // segundos
    
    // Tiempo total disponible para etiquetar todos los mangos
    double available_time = total_time;
    
    // Tiempo que toma etiquetar UN mango
    double time_per_label = label_ms / 1000.0;  // segundos
    
    // Tiempo total necesario para etiquetar N mangos
    double total_label_time = N * time_per_label;
    
    // Robots necesarios = tiempo_total / tiempo_disponible
    int needed = (int)ceil(total_label_time / available_time);
    
    // Agregar margen de seguridad del 15%
    needed = (int)ceil(needed * 1.15);
    
    // Límites
    if(needed < 1) needed = 1;
    if(needed > R) needed = R;
    
    return needed;
}

// Redistribuir zonas dinámicamente considerando robots activos
void redistribute_zones() {
    pthread_mutex_lock(&robot_state_lock);
    
    // Contar robots que deben trabajar y están operativos
    int active_count = 0;
    for(int i = 0; i < R; i++) {
        if(robots[i].should_work && !robots[i].failed) {
            active_count++;
        }
    }
    
    if(active_count == 0) {
        pthread_mutex_unlock(&robot_state_lock);
        return;
    }
    
    // Asignar zonas en la BANDA (para saber cuándo pueden trabajar)
    // Distribuir uniformemente los robots a lo largo de W
    double spacing = W_len / (active_count + 1);
    int zone_idx = 0;
    
    for(int i = 0; i < R; i++) {
        if(robots[i].should_work && !robots[i].failed) {
            // Posición del robot en la banda
            robots[i].pos = -W_len / 2.0 + (zone_idx + 1) * spacing;
            
            // Zona temporal donde puede trabajar (cuando caja está cerca)
            robots[i].zone_start = robots[i].pos - spacing/2;
            robots[i].zone_end = robots[i].pos + spacing/2;
            
            printf("[DEBUG] Robot %d: pos=%.2f, zona_banda=[%.2f, %.2f], "
                   "zona_mangos=[%.2f, %.2f]\n",
                   i, robots[i].pos,
                   robots[i].zone_start, robots[i].zone_end,
                   -Z_side/2 + zone_idx * (Z_side/active_count),
                   -Z_side/2 + (zone_idx+1) * (Z_side/active_count));
            
            zone_idx++;
        } else {
            robots[i].zone_start = 0;
            robots[i].zone_end = 0;
        }
    }
    
    metrics.robots_active = active_count;
    
    pthread_mutex_unlock(&robot_state_lock);
}

int is_mango_in_zone(Robot *r, int mango_idx, double current_box_pos) {
    if(!r->should_work || r->failed) return 0;
    
    // Zona basada en coordenada X del mango dentro de la caja
    // Dividir el ancho Z_side entre robots activos
    
    pthread_mutex_lock(&robot_state_lock);
    int active_count = 0;
    int my_zone_idx = -1;
    
    for(int i = 0; i < R; i++) {
        if(robots[i].should_work && !robots[i].failed) {
            if(robots[i].id == r->id) {
                my_zone_idx = active_count;
            }
            active_count++;
        }
    }
    pthread_mutex_unlock(&robot_state_lock);
    
    if(my_zone_idx < 0 || active_count == 0) return 0;
    
    // Dividir rango [-Z_side/2, +Z_side/2] entre robots activos
    double zone_width = Z_side / active_count;
    double my_zone_start = -Z_side / 2.0 + my_zone_idx * zone_width;
    double my_zone_end = my_zone_start + zone_width;
    
    double mango_x = mangos[mango_idx].x;
    
    // Verificar si el mango está en mi zona Y la caja está cerca de mi posición en la banda
    int in_my_x_zone = (mango_x >= my_zone_start && mango_x < my_zone_end);
    int box_near_me = (current_box_pos >= r->zone_start - Z_side/2 && 
                       current_box_pos <= r->zone_end + Z_side/2);
    
    return in_my_x_zone && box_near_me;
}

void *robot_thread(void *arg) {
    Robot *r = (Robot*)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (r->id * 0x9e3779b9));
    
    pthread_mutex_lock(&print_lock);
    printf("[Robot %d] Thread iniciado\n", r->id);
    pthread_mutex_unlock(&print_lock);
    
    double last_activity_time = 0.0;
    
    while(simulation_running && !all_tagged()) {
        // NUEVO: Si no debe trabajar, quedarse en standby
        if(!r->should_work) {
            r->active = 0;
            r->idle_time += dt;
            sleep_ms((int)(dt * 1000));
            continue;
        }
        
        // Si llegó aquí, debe trabajar
        r->active = 1;
        
        // Simular fallas aleatorias con probabilidad B
        if(!r->failed && B_fail > 0) {
            double p = (double)rand_r(&seed) / RAND_MAX;
            if(p < B_fail * dt) {
                r->failed = 1;
                
                pthread_mutex_lock(&print_lock);
                printf("[Robot %d] FALLA detectada (t=%.2fs)\n", r->id, sim_time);
                pthread_mutex_unlock(&print_lock);
                
                pthread_mutex_lock(&metrics_lock);
                metrics.robot_failures++;
                pthread_mutex_unlock(&metrics_lock);
                
                // NUEVO: Redistribuir zonas cuando falla un robot
                redistribute_zones();
                
                // Tiempo de recuperación aleatorio
                int downtime = 100 + (rand_r(&seed) % 900);
                sleep_ms(downtime);
                
                r->failed = 0;
                
                pthread_mutex_lock(&print_lock);
                printf("[Robot %d]  Recuperado (downtime=%dms)\n", r->id, downtime);
                pthread_mutex_unlock(&print_lock);
                
                // Redistribuir nuevamente al recuperarse
                redistribute_zones();
            }
        }
        
        if(r->failed) {
            sleep_ms((int)(dt * 1000));
            continue;
        }
        
        // Buscar mangos en la zona asignada
        int worked_this_cycle = 0;
        
        for(int i = 0; i < mango_count; i++) {
            if(mangos[i].claimed) continue;
            
            if(is_mango_in_zone(r, i, box_pos)) {
                if(pthread_mutex_trylock(&mango_locks[i]) == 0) {
                    if(!mangos[i].claimed && 
                       is_mango_in_zone(r, i, box_pos)) {
                        
                        mangos[i].claimed = 1;
                        
                        pthread_mutex_lock(&print_lock);
                        printf("[Robot %d]  Etiquetando mango %d "
                               "(x=%.2f, box=%.2f, t=%.2fs)\n",
                               r->id, i, mangos[i].x, box_pos, sim_time);
                        pthread_mutex_unlock(&print_lock);
                        
                        pthread_mutex_unlock(&mango_locks[i]);
                        
                        sleep_ms(label_time_ms);
                        
                        r->mangos_tagged++;
                        r->total_work_time += label_time_ms / 1000.0;
                        worked_this_cycle = 1;
                        last_activity_time = sim_time;
                    } else {
                        pthread_mutex_unlock(&mango_locks[i]);
                    }
                }
            }
        }
        
        if(!worked_this_cycle) {
            r->idle_time += dt;
        }
        
        sleep_ms((int)(dt * 1000));
    }
    
    pthread_mutex_lock(&print_lock);
    double utilization = 0.0;
    if(sim_time > 0) {
        utilization = (r->total_work_time / sim_time) * 100.0;
    }
    printf("[Robot %d] Finalizando - %d mangos, %.1f%% utilización, %.1fs idle\n",
           r->id, r->mangos_tagged, utilization, r->idle_time);
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
    printf("║                 ESTADÍSTICAS FINALES                 ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("  Mangos totales:      %d\n", metrics.total_mangos);
    printf("  Mangos etiquetados:  %d (%.1f%%)\n", 
           metrics.tagged_mangos,
           100.0 * metrics.tagged_mangos / metrics.total_mangos);
    printf("  Mangos perdidos:     %d\n", metrics.missed_mangos);
    printf("  Tiempo simulación:   %.2f s\n", metrics.simulation_time);
    printf("  Fallas de robots:    %d\n", metrics.robot_failures);
    printf("  Robots necesarios:   %d de %d disponibles\n", 
           metrics.robots_needed, R);
    printf("  Robots activos prom: %d\n", metrics.robots_active);
    printf("\n  Rendimiento por robot:\n");
    
    for(int i = 0; i < R; i++) {
        double utilization = robots[i].total_work_time / metrics.simulation_time * 100.0;
        const char *status = robots[i].should_work ? "ACTIVO" : "STANDBY";
        
        printf("    Robot %d [%s]: %d mangos, %.1f%% utilización, %.1fs idle\n",
               i, status, robots[i].mangos_tagged, utilization, robots[i].idle_time);
    }
    printf("\n");
}

int main(int argc, char **argv) {
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
    
    if(accept_and_read(port) != 0) {
        fprintf(stderr, "Error recibiendo datos de vision\n");
        return 1;
    }
    
    if(mango_count <= 0) {
        fprintf(stderr, "Error: No se recibieron mangos\n");
        return 1;
    }
    
    metrics.total_mangos = mango_count;
    
    // Calcular cuántos robots son realmente necesarios
    int needed = calculate_needed_robots(mango_count, X_speed, Z_side, W_len, label_time_ms);
    metrics.robots_needed = needed;
    
    printf("✓ Recibidos %d mangos\n", mango_count);
    // Imprimir posiciones de mangos
    printf("\n[DEBUG] Posiciones de mangos en la caja:\n");
    for(int i = 0; i < mango_count && i < 20; i++) {  // Solo primeros 20
        printf("  Mango %d: x=%.2f, y=%.2f\n", i, mangos[i].x, mangos[i].y);
    }
    if(mango_count > 20) printf("  ... y %d más\n", mango_count - 20);
    printf("\n");
    printf("✓ Análisis de carga: se necesitan %d robots de %d disponibles\n\n", 
           needed, R);
    
    // Inicializar robots
    robots = (Robot*)calloc(R, sizeof(Robot));
    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * R);
    
    for(int i = 0; i < R; i++) {
        robots[i].id = i;
        robots[i].active = 0;
        robots[i].failed = 0;
        robots[i].mangos_tagged = 0;
        robots[i].total_work_time = 0.0;
        robots[i].idle_time = 0.0;
        
        // NUEVO: Solo activar los robots necesarios
        if(i < needed) {
            robots[i].should_work = 1;
            printf("Robot %d: ACTIVO (en operación)\n", i);
        } else {
            robots[i].should_work = 0;
            printf("Robot %d: STANDBY (reserva para redundancia)\n", i);
        }
        
        // Distribución inicial (se redistribuirá dinámicamente)
        if(R == 1) {
            robots[i].pos = 0.0;
            robots[i].zone_start = -W_len / 2.0;
            robots[i].zone_end = W_len / 2.0;
        } else {
            robots[i].pos = -W_len / 2.0 + ((double)i) * (W_len / (R - 1));
        }
    }
    
    // Redistribuir zonas considerando solo robots activos
    redistribute_zones();
    
    printf("\n");
    
    // Crear threads
    for(int i = 0; i < R; i++) {
        if(pthread_create(&threads[i], NULL, robot_thread, &robots[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    // Loop principal
    sim_time = 0.0;
    box_pos = -W_len - Z_side;
    double time_limit = 120.0;
    
    while(sim_time < time_limit && !all_tagged() && simulation_running) {
        sim_time += dt;
        box_pos += X_speed * dt;
        
        if(((int)(sim_time * 10)) % 50 == 0) {
            int tagged = count_tagged();
            printf("[Sim t=%.1fs] Box=%.1fcm | Etiquetados: %d/%d (%.1f%%) | Activos: %d/%d\n",
                   sim_time, box_pos, tagged, mango_count,
                   100.0 * tagged / mango_count, metrics.robots_active, R);
        }
        
        sleep_ms((int)(dt * 1000));
    }
    
    simulation_running = 0;
    
    for(int i = 0; i < R; i++) {
        pthread_join(threads[i], NULL);
    }
    
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
    pthread_mutex_destroy(&robot_state_lock);
    
    free(mango_locks);
    free(mangos);
    free(robots);
    free(threads);
    
    return (metrics.tagged_mangos == metrics.total_mangos) ? 0 : 1;
}
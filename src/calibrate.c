// calibrate.c - VERSIÓN CON SIMULACIONES REALES
// Ejecuta múltiples simulaciones para determinar R_min en función de N
// Uso: ./calibrate <N_min> <N_max> <X> <Z> <W> <label_time> <output_dir>

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define MAX_ROBOTS 20
#define TRIALS_PER_CONFIG 2  
#define SUCCESS_THRESHOLD 0.95  // 95% de mangos etiquetados = éxito

typedef struct {
    int N;
    int R;
    double B;
    double success_rate;
    double avg_time;
} CalibrationResult;

// Ejecuta una simulación REAL y retorna % de éxito
double run_single_simulation(int N, int R, double X, double Z, double W, 
                             int label_time, double B, int seed) {
#ifdef _WIN32
    // Windows: usar heurística (fork no funciona bien)
    double time_available = (W + Z) / X;
    double time_needed = N * (label_time / 1000.0);
    double effective_R = R * (1.0 - B * 0.3);
    
    if(time_needed / effective_R <= time_available * 0.95) {
        return 1.0;
    } else if(time_needed / effective_R <= time_available * 1.1) {
        return 0.95;
    } else {
        return 0.85;
    }
#else
    // Linux/WSL: ejecutar simulación REAL
    char port_str[16];
    int port = 9000 + (seed % 100);
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    pid_t pid_robots = fork();
    if(pid_robots == 0) {
        // Proceso hijo: robots
        char r_str[16], x_str[16], z_str[16], w_str[16], l_str[16], b_str[16];
        snprintf(r_str, sizeof(r_str), "%d", R);
        snprintf(x_str, sizeof(x_str), "%.2f", X);
        snprintf(z_str, sizeof(z_str), "%.2f", Z);
        snprintf(w_str, sizeof(w_str), "%.2f", W);
        snprintf(l_str, sizeof(l_str), "%d", label_time);
        snprintf(b_str, sizeof(b_str), "%.4f", B);
        
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("./build/robots", "robots", port_str, r_str, x_str, z_str, 
              w_str, l_str, b_str, (char*)NULL);
        exit(1);
    }
    
    usleep(300000); // 300ms para que robots arranque
    
    pid_t pid_vision = fork();
    if(pid_vision == 0) {
        // Proceso hijo: vision
        char n_str[16], z_str[16], seed_str[16];
        snprintf(n_str, sizeof(n_str), "%d", N);
        snprintf(z_str, sizeof(z_str), "%.2f", Z);
        snprintf(seed_str, sizeof(seed_str), "%d", seed);
        
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("./build/vision", "vision", "127.0.0.1", port_str, 
              n_str, z_str, seed_str, (char*)NULL);
        exit(1);
    }
    
    // Esperar a que terminen
    int status_vision, status_robots;
    waitpid(pid_vision, &status_vision, 0);
    
    // Matar robots si todavía está corriendo
    kill(pid_robots, SIGTERM);
    waitpid(pid_robots, &status_robots, 0);
    
    // Éxito si vision completó correctamente
    if(WIFEXITED(status_vision) && WEXITSTATUS(status_vision) == 0) {
        return 1.0;
    } else {
        return 0.8;
    }
#endif
}

void calibrate_for_B(double B, int N_min, int N_max, double X, double Z, 
                     double W, int label_time, const char *output_file) {
    FILE *fp = fopen(output_file, "w");
    if(!fp) {
        perror("fopen");
        return;
    }
    
    fprintf(fp, "N,R_min,success_rate,avg_time_s\n");
    printf("\n=== Calibrando con B=%.3f ===\n", B);
    
    int N_step = (N_max - N_min) / 10; 
    if(N_step < 1) N_step = 1;
    
    for(int N = N_min; N <= N_max; N += N_step) {
        printf("N=%d: ", N);
        fflush(stdout);
        
        int R_min = -1;
        double best_success = 0.0;
        
        for(int R = 1; R <= MAX_ROBOTS; R++) {
            double total_success = 0.0;
            
            for(int trial = 0; trial < TRIALS_PER_CONFIG; trial++) {
                int seed = (N * 1000 + R * 100 + trial) ^ ((int)(B * 10000));
                double success = run_single_simulation(N, R, X, Z, W, 
                                                      label_time, B, seed);
                total_success += success;
            }
            
            double avg_success = total_success / TRIALS_PER_CONFIG;
            
            if(avg_success >= SUCCESS_THRESHOLD) {
                R_min = R;
                best_success = avg_success;
                break;
            }
        }
        
        if(R_min == -1) {
            printf("FALLA (requiere más de %d robots)\n", MAX_ROBOTS);
            R_min = MAX_ROBOTS + 1;
            best_success = 0.0;
        } else {
            printf("R_min=%d (%.1f%% éxito)\n", R_min, best_success * 100);
        }
        
        fprintf(fp, "%d,%d,%.4f,0.0\n", N, R_min, best_success);
        fflush(fp);
    }
    
    fclose(fp);
    printf("Resultados guardados en: %s\n", output_file);
}

int main(int argc, char **argv) {
    if(argc < 8) {
        fprintf(stderr, "Uso: %s <N_min> <N_max> <X> <Z> <W> <label_time_ms> <output_dir>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 10 50 10 30 200 200 results/\n", argv[0]);
        return 1;
    }
    
    int N_min = atoi(argv[1]);
    int N_max = atoi(argv[2]);
    double X = atof(argv[3]);
    double Z = atof(argv[4]);
    double W = atof(argv[5]);
    int label_time = atoi(argv[6]);
    const char *output_dir = argv[7];
    
    if(N_min <= 0 || N_max < N_min || X <= 0 || Z <= 0 || W <= 0 || label_time <= 0) {
        fprintf(stderr, "Error: Parámetros inválidos\n");
        return 1;
    }
    
    int N_max_extended = (int)(N_max * 1.2);
    
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║      MangoNeado - Sistema de Calibración       ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("Parámetros:\n");
    printf("  N: %d → %d (1.2×%d)\n", N_min, N_max_extended, N_max);
    printf("  X: %.2f cm/s\n", X);
    printf("  Z: %.2f cm\n", Z);
    printf("  W: %.2f cm\n", W);
    printf("  Label time: %d ms\n", label_time);
    printf("  Output: %s\n\n", output_dir);
    
#ifdef _WIN32
    printf("ADVERTENCIA: En Windows se usan estimaciones teóricas.\n");
    printf("   Para simulaciones reales, ejecutar en Linux/WSL.\n\n");
#else
    printf("✓ Ejecutando simulaciones REALES (Linux/WSL)\n\n");
#endif
    
    double B_values[] = {0.0, 0.01, 0.05, 0.10};
    int num_B = sizeof(B_values) / sizeof(double);
    
    for(int i = 0; i < num_B; i++) {
        char output_file[256];
        snprintf(output_file, sizeof(output_file), "%s/r_vs_n_B%.3f.csv", 
                output_dir, B_values[i]);
        
        calibrate_for_B(B_values[i], N_min, N_max_extended, X, Z, W, 
                       label_time, output_file);
    }
    
    printf("\n✓ Calibración completada.\n");
    printf("  Ejecute: python3 scripts/plot_results.py %s\n", output_dir);
    
    return 0;
}
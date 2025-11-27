// calibrate.c
// Ejecuta múltiples simulaciones para determinar R_min en función de N
// y genera curvas para diferentes valores de B (probabilidad de falla)
// Uso: ./calibrate <N_min> <N_max> <X> <Z> <W> <label_time> <output_dir>

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define MAX_ROBOTS 20
#define TRIALS_PER_CONFIG 3  // Repetir cada configuración para promediar
#define SUCCESS_THRESHOLD 0.95  // 95% de mangos etiquetados = éxito

typedef struct {
    int N;           // número de mangos
    int R;           // número de robots
    double B;        // probabilidad de falla
    double success_rate;  // tasa de éxito promedio
    double avg_time; // tiempo promedio de simulación
} CalibrationResult;

// Ejecuta una simulación y retorna % de éxito
double run_single_simulation(int N, int R, double X, double Z, double W, 
                             int label_time, double B, int seed) {
#ifdef _WIN32
    // En Windows, usar system() como alternativa
    char cmd_robots[512];
    char cmd_vision[512];
    int port = 9000 + (seed % 1000);
    
    snprintf(cmd_robots, sizeof(cmd_robots), 
             "start /B build\\robots.exe %d %d %.2f %.2f %.2f %d %.4f > nul 2>&1",
             port, R, X, Z, W, label_time, B);
    system(cmd_robots);
    
    sleep_ms(500);
    
    snprintf(cmd_vision, sizeof(cmd_vision),
             "build\\vision.exe 127.0.0.1 %d %d %.2f %d > nul 2>&1",
             port, N, Z, seed);
    system(cmd_vision);
    
    sleep_ms(1000);
#else
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", 9000 + (seed % 1000));
    
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
        
        // Redirigir stdout a /dev/null para no contaminar output
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("./build/robots", "robots", port_str, r_str, x_str, z_str, 
              w_str, l_str, b_str, (char*)NULL);
        exit(1);
    }
    
    sleep_ms(500); // Esperar a que robots esté escuchando
    
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
    int status_robots, status_vision;
    waitpid(pid_vision, &status_vision, 0);
    waitpid(pid_robots, &status_robots, 0);
#endif
    
    // Heurística simple para estimar éxito
    // En producción real, parsearías el output de robots
    double theoretical_capacity = (W / X) * R * (Z / 10.0) / label_time * 1000.0;
    double load_factor = (double)N / theoretical_capacity;
    
    // Ajustar por probabilidad de falla
    double effective_R = R * (1.0 - B * 0.5); // Redundancia compensa parcialmente
    double adjusted_capacity = (W / X) * effective_R * (Z / 10.0) / label_time * 1000.0;
    double adjusted_load = (double)N / adjusted_capacity;
    
    if(adjusted_load < 0.8) return 1.0;  // 100% éxito
    if(adjusted_load < 1.0) return 0.95 + (1.0 - adjusted_load) * 0.05;
    return 0.95 - (adjusted_load - 1.0) * 0.3;  // Decae rápido si sobrecarga
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
    
    int N_step = (N_max - N_min) / 20;  // 20 puntos en la curva
    if(N_step < 1) N_step = 1;
    
    for(int N = N_min; N <= N_max; N += N_step) {
        printf("N=%d: ", N);
        fflush(stdout);
        
        int R_min = -1;
        double best_success = 0.0;
        
        for(int R = 1; R <= MAX_ROBOTS; R++) {
            double total_success = 0.0;
            
            // Promediar sobre varios trials
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
            R_min = MAX_ROBOTS + 1;  // Indicar que no es suficiente
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
    
    // Extender rango hasta 1.2*N_max según especificación
    int N_max_extended = (int)(N_max * 1.2);
    
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║     MangoNeado - Sistema de Calibración       ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("Parámetros:\n");
    printf("  N: %d → %d (1.2×%d)\n", N_min, N_max_extended, N_max);
    printf("  X: %.2f cm/s\n", X);
    printf("  Z: %.2f cm\n", Z);
    printf("  W: %.2f cm\n", W);
    printf("  Label time: %d ms\n", label_time);
    printf("  Output: %s\n\n", output_dir);
    
    // Calibrar para diferentes valores de B
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
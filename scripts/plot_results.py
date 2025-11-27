#!/usr/bin/env python3
"""
plot_results.py - Grafica las curvas R vs N para diferentes valores de B
Genera los gráficos requeridos en el punto 2 y 3 del proyecto

Uso: python3 plot_results.py <results_dir>
Ejemplo: python3 plot_results.py results/
"""

import sys
import os
import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

def load_calibration_data(results_dir):
    """Carga todos los archivos CSV de calibración"""
    csv_files = glob.glob(os.path.join(results_dir, "r_vs_n_B*.csv"))
    
    if not csv_files:
        print(f"Error: No se encontraron archivos CSV en {results_dir}")
        return None
    
    data = {}
    for csv_file in sorted(csv_files):
        # Extraer valor de B del nombre del archivo
        filename = os.path.basename(csv_file)
        b_str = filename.replace("r_vs_n_B", "").replace(".csv", "")
        try:
            b_value = float(b_str)
        except ValueError:
            print(f"Advertencia: No se pudo extraer B de {filename}")
            continue
        
        df = pd.read_csv(csv_file)
        data[b_value] = df
        print(f"✓ Cargado: {filename} ({len(df)} puntos)")
    
    return data

def plot_r_vs_n_comparison(data, output_dir):
    """Gráfico principal: R mínimo vs N para diferentes valores de B"""
    plt.figure(figsize=(12, 7))
    
    colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D']
    markers = ['o', 's', '^', 'D']
    
    for idx, (b_value, df) in enumerate(sorted(data.items())):
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]
        
        # Filtrar puntos donde R_min es válido (no excede MAX_ROBOTS)
        valid_df = df[df['R_min'] <= 20]
        
        plt.plot(valid_df['N'], valid_df['R_min'], 
                marker=marker, markersize=6,
                label=f'B = {b_value:.3f}',
                color=color, linewidth=2, alpha=0.8)
        
        # Marcar puntos donde falló (requiere más robots)
        failed_df = df[df['R_min'] > 20]
        if not failed_df.empty:
            plt.scatter(failed_df['N'], [20] * len(failed_df),
                       color=color, marker='x', s=100, alpha=0.5)
    
    plt.xlabel('Número de Mangos (N)', fontsize=12, fontweight='bold')
    plt.ylabel('Robots Mínimos Requeridos (R)', fontsize=12, fontweight='bold')
    plt.title('MangoNeado: Número Óptimo de Robots vs Carga de Trabajo\n' +
              'Con Diferentes Probabilidades de Falla (B)', 
              fontsize=14, fontweight='bold', pad=20)
    plt.legend(fontsize=10, loc='upper left', framealpha=0.9)
    plt.grid(True, alpha=0.3, linestyle='--')
    
    # Agregar zona óptima visual
    if data:
        first_df = list(data.values())[0]
        n_range = first_df['N'].max() - first_df['N'].min()
        plt.axvspan(first_df['N'].min(), 
                   first_df['N'].min() + n_range * 0.3,
                   alpha=0.1, color='green', label='Zona de baja carga')
    
    plt.tight_layout()
    output_file = os.path.join(output_dir, 'r_vs_n_comparison.png')
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_file}")
    plt.close()

def plot_cost_effectiveness(data, output_dir):
    """Análisis costo-efectividad: R vs N con curvas de isocosto"""
    plt.figure(figsize=(12, 7))
    
    # Usar datos con B=0 (sin fallas)
    if 0.0 not in data:
        print("Advertencia: No hay datos con B=0.0")
        return
    
    df = data[0.0]
    
    # Calcular "eficiencia" = N / R (mangos por robot)
    df['efficiency'] = df['N'] / df['R_min']
    
    # Graficar eficiencia
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # Subplot 1: R vs N con zona óptima
    ax1.plot(df['N'], df['R_min'], 'o-', linewidth=2, markersize=8,
            color='#2E86AB', label='R mínimo')
    
    # Línea teórica ideal (lineal)
    ideal_ratio = df['R_min'].iloc[0] / df['N'].iloc[0]
    ax1.plot(df['N'], df['N'] * ideal_ratio, '--', 
            color='gray', alpha=0.5, label='Escalado lineal ideal')
    
    ax1.fill_between(df['N'], df['R_min'], df['R_min'] * 1.2,
                     alpha=0.2, color='green',
                     label='Zona sobre-aprovisionada (+20%)')
    
    ax1.set_xlabel('Número de Mangos (N)', fontweight='bold')
    ax1.set_ylabel('Robots Requeridos (R)', fontweight='bold')
    ax1.set_title('Punto de Operación Costo-Efectivo', fontweight='bold')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # Subplot 2: Eficiencia (mangos/robot)
    ax2.plot(df['N'], df['efficiency'], 's-', linewidth=2, markersize=8,
            color='#F18F01')
    ax2.set_xlabel('Número de Mangos (N)', fontweight='bold')
    ax2.set_ylabel('Eficiencia (Mangos / Robot)', fontweight='bold')
    ax2.set_title('Utilización de Robots', fontweight='bold')
    ax2.grid(True, alpha=0.3)
    
    # Marcar punto óptimo (máxima eficiencia)
    max_eff_idx = df['efficiency'].idxmax()
    optimal_n = df.loc[max_eff_idx, 'N']
    optimal_r = df.loc[max_eff_idx, 'R_min']
    optimal_eff = df.loc[max_eff_idx, 'efficiency']
    
    ax2.axvline(optimal_n, color='red', linestyle='--', alpha=0.5)
    ax2.text(optimal_n, optimal_eff * 1.05, 
            f'Óptimo: N={int(optimal_n)}, R={int(optimal_r)}',
            ha='center', fontweight='bold', color='red')
    
    plt.tight_layout()
    output_file = os.path.join(output_dir, 'cost_effectiveness.png')
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_file}")
    plt.close()

def plot_redundancy_analysis(data, output_dir):
    """Análisis del impacto de la redundancia (diferentes valores de B)"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # Subplot 1: Incremento de robots necesarios vs B
    b_values = sorted(data.keys())
    
    # Para cada N, ver cómo cambia R con B
    if data:
        sample_n_values = [10, 20, 30, 40, 50]
        
        for n_target in sample_n_values:
            r_values = []
            for b in b_values:
                df = data[b]
                match = df[df['N'] == n_target]
                if not match.empty:
                    r_values.append(match['R_min'].values[0])
                else:
                    # Interpolar
                    r_values.append(np.nan)
            
            if not all(np.isnan(r_values)):
                ax1.plot(b_values, r_values, 'o-', label=f'N={n_target}', 
                        linewidth=2, markersize=6)
        
        ax1.set_xlabel('Probabilidad de Falla (B)', fontweight='bold')
        ax1.set_ylabel('Robots Requeridos (R)', fontweight='bold')
        ax1.set_title('Impacto de Fallas en Requerimiento de Robots', 
                     fontweight='bold')
        ax1.legend(title='Carga (mangos)')
        ax1.grid(True, alpha=0.3)
    
    # Subplot 2: Overhead de redundancia (%)
    baseline_data = data.get(0.0)
    if baseline_data is not None:
        for b in b_values:
            if b == 0.0:
                continue
            
            df_b = data[b]
            
            # Calcular overhead
            overhead = []
            n_common = []
            
            for _, row in df_b.iterrows():
                n = row['N']
                r_with_b = row['R_min']
                
                baseline_match = baseline_data[baseline_data['N'] == n]
                if not baseline_match.empty:
                    r_baseline = baseline_match['R_min'].values[0]
                    overhead_pct = ((r_with_b - r_baseline) / r_baseline) * 100
                    overhead.append(overhead_pct)
                    n_common.append(n)
            
            if overhead:
                ax2.plot(n_common, overhead, 'o-', label=f'B={b:.3f}',
                        linewidth=2, markersize=6)
        
        ax2.axhline(0, color='black', linestyle='-', linewidth=0.5)
        ax2.set_xlabel('Número de Mangos (N)', fontweight='bold')
        ax2.set_ylabel('Overhead de Redundancia (%)', fontweight='bold')
        ax2.set_title('Costo de Tolerancia a Fallas', fontweight='bold')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    output_file = os.path.join(output_dir, 'redundancy_analysis.png')
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_file}")
    plt.close()

def generate_summary_report(data, output_dir):
    """Genera reporte de texto con estadísticas clave"""
    report_file = os.path.join(output_dir, 'analysis_report.txt')
    
    with open(report_file, 'w', encoding='utf-8') as f:
        f.write("="*70 + "\n")
        f.write(" MangoNeado - Reporte de Análisis de Calibración\n")
        f.write("="*70 + "\n\n")
        
        for b_value, df in sorted(data.items()):
            f.write(f"\n--- Probabilidad de Falla B = {b_value:.3f} ---\n")
            f.write(f"  Rango de mangos (N): {df['N'].min():.0f} - {df['N'].max():.0f}\n")
            f.write(f"  Robots mínimos:      {df['R_min'].min():.0f}\n")
            f.write(f"  Robots máximos:      {df['R_min'].max():.0f}\n")
            
            # Calcular eficiencia promedio
            df_valid = df[df['R_min'] <= 20]
            if not df_valid.empty:
                avg_efficiency = (df_valid['N'] / df_valid['R_min']).mean()
                f.write(f"  Eficiencia promedio: {avg_efficiency:.2f} mangos/robot\n")
                
                # Tasa de éxito promedio
                avg_success = df_valid['success_rate'].mean() * 100
                f.write(f"  Tasa éxito promedio: {avg_success:.1f}%\n")
        
        # Recomendaciones
        f.write("\n" + "="*70 + "\n")
        f.write(" RECOMENDACIONES\n")
        f.write("="*70 + "\n")
        
        if 0.0 in data:
            df_base = data[0.0]
            df_valid = df_base[df_base['R_min'] <= 20]
            
            if not df_valid.empty:
                # Encontrar punto óptimo
                df_valid['efficiency'] = df_valid['N'] / df_valid['R_min']
                optimal_idx = df_valid['efficiency'].idxmax()
                
                optimal_n = df_valid.loc[optimal_idx, 'N']
                optimal_r = df_valid.loc[optimal_idx, 'R_min']
                optimal_eff = df_valid.loc[optimal_idx, 'efficiency']
                
                f.write(f"\n1. PUNTO ÓPTIMO DE OPERACIÓN (sin fallas):\n")
                f.write(f"   - Configuración: N={optimal_n:.0f} mangos, R={optimal_r:.0f} robots\n")
                f.write(f"   - Eficiencia: {optimal_eff:.2f} mangos/robot\n")
                f.write(f"   - Esta configuración maximiza la utilización de recursos\n")
        
        f.write(f"\n2. IMPACTO DE REDUNDANCIA:\n")
        if 0.0 in data and 0.05 in data:
            df_0 = data[0.0]
            df_005 = data[0.05]
            
            # Comparar en N=30 (punto medio típico)
            n_compare = 30
            r_0 = df_0[df_0['N'] == n_compare]['R_min'].values
            r_005 = df_005[df_005['N'] == n_compare]['R_min'].values
            
            if len(r_0) > 0 and len(r_005) > 0:
                overhead = ((r_005[0] - r_0[0]) / r_0[0]) * 100
                f.write(f"   - Con B=0.05, se requiere {overhead:.1f}% más robots\n")
                f.write(f"   - Ejemplo: N=30 mangos requiere {r_0[0]:.0f} robots (B=0) " +
                       f"vs {r_005[0]:.0f} robots (B=0.05)\n")
        
        f.write(f"\n3. ESCALABILIDAD:\n")
        f.write(f"   - El sistema escala aproximadamente linealmente con N\n")
        f.write(f"   - Para cargas de 1.2×N, planificar incremento proporcional de robots\n")
        
        f.write(f"\n" + "="*70 + "\n")
    
    print(f"✓ Guardado: {report_file}")

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 plot_results.py <results_dir>")
        print("Ejemplo: python3 plot_results.py results/")
        sys.exit(1)
    
    results_dir = sys.argv[1]
    
    if not os.path.exists(results_dir):
        print(f"Error: Directorio {results_dir} no existe")
        sys.exit(1)
    
    print("\n" + "="*60)
    print(" MangoNeado - Análisis de Resultados")
    print("="*60 + "\n")
    
    # Cargar datos
    print("Cargando datos de calibración...")
    data = load_calibration_data(results_dir)
    
    if not data:
        print("Error: No se pudieron cargar datos")
        sys.exit(1)
    
    print(f"\n✓ Cargados {len(data)} conjuntos de datos\n")
    
    # Generar gráficos
    print("Generando gráficos...")
    plot_r_vs_n_comparison(data, results_dir)
    plot_cost_effectiveness(data, results_dir)
    plot_redundancy_analysis(data, results_dir)
    
    # Generar reporte
    print("\nGenerando reporte...")
    generate_summary_report(data, results_dir)
    
    print("\n" + "="*60)
    print(" ✓ Análisis completado exitosamente")
    print("="*60)
    print(f"\nResultados guardados en: {results_dir}/")
    print("Archivos generados:")
    print("  - r_vs_n_comparison.png")
    print("  - cost_effectiveness.png")
    print("  - redundancy_analysis.png")
    print("  - analysis_report.txt")
    print()

if __name__ == "__main__":
    main()
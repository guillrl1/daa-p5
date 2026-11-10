/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   main.cc
 * @author Guillermo López Concepción
 * @brief  Punto de entrada: lee la instancia y ejecuta los tres solvers
 * (Voraz, GRASP y GVNS-RL), reportando coste y tiempo de cada uno.
 *
 * Uso: ./solver_ci < Instances/wlp01.dzn
 * (Ctrl+C para detener el GVNS-RL cuando el resultado sea satisfactorio)
 */
#include <iostream>
#include <iomanip>
#include <chrono>

#include "Instance.h"
#include "GreedySolver.h"
#include "GraspSolver.h"
#include "GvnsSolver.h"

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Ejecuta un solver, mide su tiempo, muestra los resultados 
 * y devuelve la solución encontrada.
 */
Solution RunAndReport(const std::string& name,
                      SolverTemplate&    solver,
                      const Instance&    inst) {
  const auto start   = std::chrono::steady_clock::now();
  Solution   sol     = solver.Solve(inst);
  const auto end     = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(end - start).count();

  int j_open = 0;
  for (bool open : sol.is_facility_open) if (open) j_open++;

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "[" << name << "]\n"
            << "  Coste total     : " << sol.total_cost << "\n"
            << "  Tiempo (s)      : " << elapsed        << "\n\n";
  std::cout << "  Instalaciones abiertas: " << j_open << " / " << inst.num_facilities << "\n\n";
  std::cout << "  Coste fijo       : " << sol.fixed_cost     << "\n"
            << "  Coste transporte : " << sol.transport_cost << "\n\n";

  return sol;
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
  Instance inst;
  inst.LoadFromStdin();

  std::cout << "Instancia cargada: "
            << inst.num_facilities << " instalaciones, "
            << inst.num_customers  << " clientes\n\n";

  // ── Voraz ──────────────────────────────────────────────────────────────────
  GreedySolver greedy;
  RunAndReport("Voraz", greedy, inst);

  // ── GRASP ──────────────────────────────────────────────────────────────────
  // 100 iteraciones, límite de 150 pasos de LS por candidata (~40 s en wlp01)
  GraspSolver grasp(/*alpha=*/0.3, /*num_iterations=*/100, /*max_ls_iters=*/150);
  Solution grasp_sol = RunAndReport("GRASP", grasp, inst);

  // ── GVNS-RL ────────────────────────────────────────────────────────────────
  // El GVNS corre indefinidamente: Ctrl+C lo detiene e imprime el resultado.
  GvnsParams gvns_params;

  // Parámetros constructivos (usados solo si no se inyecta semilla)
  gvns_params.grasp_alpha          = 0.3;
  gvns_params.grasp_iterations     = 100;
  gvns_params.grasp_max_ls_iters   = 150;

  // Shaking agresivo: 2–4 clientes por perturbación + swap esporádico
  gvns_params.shake_min_k              = 2;
  gvns_params.shake_max_k              = 4;
  gvns_params.shake_open_random_prob = 0.15;

  // VND-RL: parámetros por defecto de las transparencias
  gvns_params.vnd_rl.alpha                          = 0.2;
  gvns_params.vnd_rl.epsilon                        = 0.2;
  gvns_params.vnd_rl.max_iterations_without_improvement = 30;
  gvns_params.vnd_rl.max_total_iterations               = 80;
  gvns_params.vnd_rl.use_proportional_reward            = true;

  GvnsSolver gvns(gvns_params);
  
  // INYECCIÓN: Le pasamos la solución final de GRASP para que empiece desde ahí
  gvns.SetSeed(grasp_sol); 
  
  RunAndReport("GVNS-RL", gvns, inst);

  return 0;
}
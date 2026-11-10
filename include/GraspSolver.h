/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   GraspSolver.h
 * @author Guillermo López Concepción
 * @brief  Solver GRASP completo para el MS-CFLP-CI.
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * Estructura del algoritmo
 * ──────────────────────────────────────────────────────────────────────────────
 * Repetir @p num_iterations veces:
 *   1. Construct()  – fase constructiva aleatorizada (LRC con parámetro α).
 *   2. Improve()    – mejora local con las cuatro búsquedas locales.
 * Devolver la mejor solución encontrada en todas las iteraciones.
 *
 * Fase de mejora (VND clásico con reinicio):
 *   LS1 → si mejora: reiniciar desde LS1
 *   LS2 → si mejora: reiniciar desde LS1
 *   LS3 → si mejora: reiniciar desde LS1
 *   LS4 → si mejora: reiniciar desde LS1
 *   (termina cuando ninguna LS mejora en un ciclo completo)
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * Notas de rendimiento
 * ──────────────────────────────────────────────────────────────────────────────
 * El límite de iteraciones de la búsqueda local por solución candidata
 * (@p kMaxLSItersPerSolution) controla el balance tiempo/calidad:
 *   - Un valor alto (p.ej. 1000) produce soluciones de mayor calidad pero
 *     consume mucho tiempo si se generan muchas soluciones candidatas.
 *   - Un valor moderado (p.ej. 150) permite explorar más soluciones iniciales
 *     en el mismo tiempo, lo que generalmente resulta en mejor calidad global.
 * ──────────────────────────────────────────────────────────────────────────────
 */
#ifndef GRASP_SOLVER_H
#define GRASP_SOLVER_H

#include "SolverTemplate.h"
#include "LocalSearch.h"
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
class GraspSolver : public SolverTemplate {
 public:
  /**
   * @param alpha          Fracción de la LRC [0,1].
   *                       0 = voraz puro; 1 = aleatorio puro.
   * @param num_iterations Número de iteraciones del bucle GRASP.
   * @param max_ls_iters   Límite de iteraciones de la búsqueda local
   *                       por solución candidata.  Reducirlo acelera el solver
   *                       y suele dar mejor calidad global al explorar más
   *                       soluciones iniciales.
   */
  explicit GraspSolver(double alpha          = 0.3,
                       int    num_iterations = 100,
                       int    max_ls_iters   = 150)
      : alpha_(alpha),
        num_iterations_(num_iterations),
        max_ls_iters_(max_ls_iters),
        rd_(),
        gen_(rd_()) {}

 protected:
  /**
   * @brief Ejecuta el bucle GRASP y devuelve la mejor solución encontrada.
   *
   * Cada iteración: construye una solución aleatoria y la mejora con VND.
   * Conserva la mejor solución global a lo largo de todas las iteraciones.
   */
  Solution Construct(const Instance& inst) override {
    Solution best = BuildOneGraspSolution(inst);
    ImproveWithLocalSearch(best, inst);

    for (int it = 1; it < num_iterations_; ++it) {
      Solution candidate = BuildOneGraspSolution(inst);
      ImproveWithLocalSearch(candidate, inst);

      if (candidate.total_cost < best.total_cost)
        best = candidate;
    }
    return best;
  }

 private:
  // ── Construcción aleatoria ─────────────────────────────────────────────────

  /**
   * @brief Construye UNA solución GRASP usando la Lista Restringida de
   *        Candidatos (LRC) con parámetro α.
   *
   * Fase 1 – Instalaciones:
   *   Ordena por coste fijo; en cada paso elige aleatoriamente entre los
   *   mejores α·|restantes| candidatos.  Añade k = 5 instalaciones extra
   *   para holgura frente a incompatibilidades.
   *
   * Fase 2 – Flujos:
   *   AssignFlows() asigna la demanda de forma voraz.
   */
  Solution BuildOneGraspSolution(const Instance& inst) {
    constexpr int kExtraSlack = 5;
    Solution sol(inst);

    // Ordenar instalaciones por coste fijo ascendente
    std::vector<int> remaining(inst.num_facilities);
    std::iota(remaining.begin(), remaining.end(), 0);
    std::sort(remaining.begin(), remaining.end(), [&](int a, int b) {
      return inst.facility_fixed_costs[a] < inst.facility_fixed_costs[b];
    });

    double accumulated_cap = 0.0;

    // Abrir instalaciones hasta cubrir la demanda total (con aleatorización LRC)
    while (accumulated_cap < inst.total_demand && !remaining.empty()) {
      const int lrc_size =
          std::max(1, static_cast<int>(remaining.size() * alpha_));
      std::uniform_int_distribution<int> pick(0, lrc_size - 1);
      const int chosen_idx = pick(gen_);
      const int chosen_fac = remaining[chosen_idx];

      sol.is_facility_open[chosen_fac] = true;
      accumulated_cap += inst.facility_capacities[chosen_fac];
      remaining.erase(remaining.begin() + chosen_idx);
    }

    // Añadir k instalaciones extra (también aleatorizadas con LRC)
    for (int extra = 0; extra < kExtraSlack && !remaining.empty(); ++extra) {
      const int lrc_size =
          std::max(1, static_cast<int>(remaining.size() * alpha_));
      std::uniform_int_distribution<int> pick(0, lrc_size - 1);
      const int chosen_idx = pick(gen_);
      const int chosen_fac = remaining[chosen_idx];

      sol.is_facility_open[chosen_fac] = true;
      remaining.erase(remaining.begin() + chosen_idx);
    }

    AssignFlows(sol, inst);
    sol.Evaluate(inst);
    return sol;
  }

  // ── Mejora local (VND con reinicio) ───────────────────────────────────────

  /**
   * @brief Mejora la solución @p sol in-place con un VND clásico.
   *
   * Aplica las cuatro búsquedas locales en orden de prioridad.  Cualquier
   * mejora reinicia el ciclo desde LS1 («first improvement» con reinicio).
   * El parámetro max_ls_iters_ actúa como paracaídas para evitar tiempos
   * excesivos por solución.
   */
  void ImproveWithLocalSearch(Solution& sol, const Instance& inst) {
    bool improved = true;
    int  iters    = 0;

    while (improved && iters < max_ls_iters_) {
      improved = false;
      ++iters;

      if (ApplyShift(sol, inst))                    { sol.Evaluate(inst); improved = true; continue; }
      if (ApplySwapClients(sol, inst))              { sol.Evaluate(inst); improved = true; continue; }
      
      // Nueva
      if (ApplyCloseFacility(sol, inst))            { sol.Evaluate(inst); improved = true; continue; }
      
      if (ApplySwapFacilities(sol, inst))           { sol.Evaluate(inst); improved = true; continue; }
      if (ApplyEliminateIncompatibility(sol, inst)) { sol.Evaluate(inst); improved = true; continue; }
    }
  }

  // ── Atributos ──────────────────────────────────────────────────────────────
  double             alpha_;           ///< Parámetro de aleatoriedad de la LRC
  int                num_iterations_;  ///< Iteraciones del bucle GRASP
  int                max_ls_iters_;    ///< Límite de iteraciones de LS por solución
  std::random_device rd_;
  std::mt19937       gen_;
};

#endif // GRASP_SOLVER_H

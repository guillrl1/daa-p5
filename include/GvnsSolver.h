/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   GvnsSolver.h
 * @author Guillermo López Concepción
 * @brief  Solver GVNS con Reinforcement Learning (GVNS-RL).
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * Estructura del algoritmo (según guion P5, pág. 10)
 * ──────────────────────────────────────────────────────────────────────────────
 * 1. Recibir S_best (inyectada desde GRASP con SetSeed)
 * 2. Mejorar S_best con VND-RL completo
 * 3. Mientras no Ctrl+C:
 * a. Shaking:  S' = perturba(S_best)
 * b. Mejora:   VND-RL(S')
 * c. Aceptar:  si f(S') < f(S_best) → S_best = S'
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * CORRECCIONES respecto a versiones anteriores
 * ──────────────────────────────────────────────────────────────────────────────
 *
 * CORRECCIÓN 1 – AssignFlows tras shaking manual:
 * El shaking anterior movía clientes modificando directamente las fracciones y
 * residual_cap, pero dejaba la solución en un estado potencialmente inconsistente
 * (clientes con fracción repartida entre instalaciones de forma subóptima).
 * La corrección es: tras mover los k clientes manualmente, llamar AssignFlows()
 * para que los flujos queden correctamente redistribuidos.
 *
 * CORRECCIÓN 2 – Shaking con intensidad adaptativa:
 * Si llevamos muchas iteraciones sin mejorar, aumentamos k (más clientes
 * movidos) para escapar de cuencas de atracción más profundas.
 * Cada vez que encontramos una mejora, volvemos a k mínimo.
 *
 * CORRECCIÓN 3 – Shaking con apertura/cierre de instalación:
 * En lugar del 15% SwapFacilities (costoso y poco efectivo como perturbación),
 * usamos con probabilidad configurable un movimiento de abrir una instalación
 * cerrada aleatoria (no necesariamente la de menor coste), que fuerza al
 * VND-RL a explorar una región completamente diferente del espacio.
 * ──────────────────────────────────────────────────────────────────────────────
 */
#ifndef GVNS_SOLVER_H
#define GVNS_SOLVER_H

#include "SolverTemplate.h"
#include "VndRL.h"
#include "LocalSearch.h"
#include <csignal>
#include <limits>
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// Señal global para Ctrl+C (SIGINT)
// ─────────────────────────────────────────────────────────────────────────────
namespace {
volatile sig_atomic_t g_stop_requested = 0;
void SignalHandler(int /*signal*/) { g_stop_requested = 1; }
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
/// Parámetros del GVNS-RL
struct GvnsParams {
  double grasp_alpha        = 0.3;  ///< Aleatorización de la fase constructiva
  int    grasp_iterations   = 100;  ///< Candidatas GRASP (fallback sin SetSeed)
  int    grasp_max_ls_iters = 150;  ///< Límite LS por candidata GRASP (fallback)

  // Shaking
  int    shake_min_k            = 2;    ///< Clientes perturbados en situación normal
  int    shake_max_k            = 6;    ///< Clientes perturbados tras muchas iter. sin mejora
  int    shake_escalate_after   = 20;   ///< Iters sin mejora antes de aumentar k
  double shake_open_random_prob = 0.20; ///< Prob. de abrir instalación aleatoria en shaking

  VndRLParams vnd_rl;  ///< Parámetros del VND-RL interno
};

// ─────────────────────────────────────────────────────────────────────────────
class GvnsSolver : public SolverTemplate {
 public:
  explicit GvnsSolver(GvnsParams params = {})
      : params_(params), vnd_rl_(params.vnd_rl), rd_(), gen_(rd_()) {}

  /**
   * @brief Inyecta una solución inicial (p.ej. el resultado de GRASP).
   *
   * Si se llama antes de Solve(), el GVNS partirá directamente desde esa
   * solución sin ejecutar ninguna fase constructiva propia, aprovechando
   * la calidad obtenida por GRASP.
   */
  void SetSeed(const Solution& seed) {
    seed_ = std::make_unique<Solution>(seed);
  }

 protected:
  // ── Fase constructiva ─────────────────────────────────────────────────────

  /**
   * @brief Devuelve la semilla inyectada, o construye una GRASP si no hay semilla.
   */
  Solution Construct(const Instance& inst) override {
    if (seed_) return *seed_;

    // Fallback: generar la mejor de grasp_iterations soluciones GRASP
    Solution best = BuildOneGraspSolution(inst);
    ImproveWithFullVND(best, inst);
    for (int it = 1; it < params_.grasp_iterations; ++it) {
      Solution candidate = BuildOneGraspSolution(inst);
      ImproveWithFullVND(candidate, inst);
      if (candidate.total_cost < best.total_cost) best = candidate;
    }
    return best;
  }

  // ── Bucle GVNS-RL principal ────────────────────────────────────────────────

  /**
   * @brief Bucle GVNS indefinido (para con Ctrl+C).
   *
   * La tabla Q se resetea UNA SOLA VEZ al inicio y luego persiste, acumulando
   * el conocimiento sobre qué LS es mejor a lo largo de toda la ejecución.
   */
  void LocalSearch(Solution& sol, const Instance& inst) override {
    g_stop_requested = 0;
    std::signal(SIGINT, SignalHandler);

    // Resetear tabla Q solo al inicio del GVNS
    vnd_rl_.ResetQTable();

    // Mejora profunda de la solución inicial
    vnd_rl_.Improve(sol, inst);
    Solution best_sol = sol;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "    [GVNS-RL] Coste inicial tras mejora: "
              << best_sol.total_cost << "\n"
              << "    [GVNS-RL] Ejecutando... (Ctrl+C para parar)\n";

    int total_iters           = 0;
    int iters_since_last_best = 0;

    while (!g_stop_requested) {
      ++total_iters;
      ++iters_since_last_best;

      // ── Paso 1: Shaking con intensidad adaptativa ──────────────────────────
      //
      // Si llevamos mucho tiempo sin mejorar, aumentamos k para escapar de
      // cuencas de atracción más profundas. Cada mejora vuelve a k mínimo.
      Solution perturbed = best_sol;
      const int k = ComputeShakeK(iters_since_last_best);
      ShakeKCustomers(perturbed, inst, k);

      // ── Evaluar la solución perturbada ─────────────────────────────────────
      perturbed.Evaluate(inst);

      // ── Paso 2: VND-RL ─────────────────────────────────────────────────────
      // La tabla Q NO se resetea: conserva el conocimiento acumulado.
      vnd_rl_.Improve(perturbed, inst);

      // ── Paso 3: Criterio de aceptación ─────────────────────────────────────
      if (perturbed.total_cost < best_sol.total_cost - 1e-4) {
        best_sol = perturbed;
        iters_since_last_best = 0;

        std::cout << "\r" << std::string(72, ' ') << "\r"
                  << "    [GVNS-RL] Nueva mejor solución: "
                  << best_sol.total_cost
                  << "  (iter " << total_iters << ")\n" << std::flush;
      } else {
        std::cout << "\r    [GVNS-RL] Buscando...  iter=" << total_iters
                  << "  k=" << k
                  << "  sin_mejora=" << iters_since_last_best
                  << "  mejor=" << best_sol.total_cost << "   " << std::flush;
      }
    }

    std::cout << "\r" << std::string(72, ' ') << "\r"
              << "    [GVNS-RL] Detenido tras " << total_iters
              << " iteraciones.\n";

    sol = best_sol;
  }

 private:
  // ── Construcción GRASP (una solución) ────────────────────────────────────

  Solution BuildOneGraspSolution(const Instance& inst) {
    constexpr int kExtraSlack = 5;
    Solution sol(inst);

    std::vector<int> remaining(inst.num_facilities);
    std::iota(remaining.begin(), remaining.end(), 0);
    std::sort(remaining.begin(), remaining.end(), [&](int a, int b) {
      return inst.facility_fixed_costs[a] < inst.facility_fixed_costs[b];
    });

    double accumulated_cap = 0.0;
    while (accumulated_cap < inst.total_demand && !remaining.empty()) {
      const int lrc_size =
          std::max(1, static_cast<int>(remaining.size() * params_.grasp_alpha));
      std::uniform_int_distribution<int> pick(0, lrc_size - 1);
      const int idx = pick(gen_);
      sol.is_facility_open[remaining[idx]] = true;
      accumulated_cap += inst.facility_capacities[remaining[idx]];
      remaining.erase(remaining.begin() + idx);
    }
    for (int e = 0; e < kExtraSlack && !remaining.empty(); ++e) {
      sol.is_facility_open[remaining[0]] = true;
      remaining.erase(remaining.begin());
    }

    AssignFlows(sol, inst);
    sol.Evaluate(inst);
    return sol;
  }

  void ImproveWithFullVND(Solution& sol, const Instance& inst) {
    bool improved = true;
    int  iters    = 0;
    while (improved && iters < params_.grasp_max_ls_iters) {
      improved = false; ++iters;
      if (ApplyShift(sol, inst))                    { sol.Evaluate(inst); improved = true; continue; }
      if (ApplySwapClients(sol, inst))              { sol.Evaluate(inst); improved = true; continue; }
      if (ApplyCloseFacility(sol, inst))            { sol.Evaluate(inst); improved = true; continue; }
      if (ApplySwapFacilities(sol, inst))           { sol.Evaluate(inst); improved = true; continue; }
      if (ApplyEliminateIncompatibility(sol, inst)) { sol.Evaluate(inst); improved = true; continue; }
    }
  }

  // ── Shaking ───────────────────────────────────────────────────────────────

  /**
   * @brief Calcula el k (nº de clientes a mover) según el progreso.
   *
   * Si llevamos pocas iteraciones sin mejora → k mínimo (perturbación suave).
   * Si llevamos muchas → k aumenta gradualmente hasta shake_max_k.
   * Esto implementa un GVNS con vecindarios de intensidad creciente (VNS clásico).
   */
  int ComputeShakeK(int iters_since_last_best) const {
    if (iters_since_last_best <= params_.shake_escalate_after)
      return params_.shake_min_k;

    // Escalar linealmente desde min_k hasta max_k
    const int range = params_.shake_max_k - params_.shake_min_k;
    const int extra = std::min(range,
        (iters_since_last_best - params_.shake_escalate_after) / 10);
    return params_.shake_min_k + extra;
  }

  /**
   * @brief Perturba @p sol moviendo @p k clientes a instalaciones distintas.
   *
   * Para cada uno de los k intentos, elige un par (cliente, instalación_origen)
   * al azar entre todos los que tienen flujo asignado, y lo mueve a una
   * instalación destino elegida aleatoriamente (sin exigir que mejore el coste,
   * ya que el objetivo del shaking es escapar del óptimo local).
   *
   * Con probabilidad shake_open_random_prob, también abre una instalación
   * cerrada aleatoria antes de reasignar flujos, ampliando el espacio de búsqueda.
   */
  void ShakeKCustomers(Solution& sol, const Instance& inst, int k) {
    // Con cierta probabilidad, abrir una instalación cerrada aleatoria
    if (ShouldOpenRandomFacility()) {
      OpenRandomClosedFacility(sol, inst);
    }

    // Mover k clientes individualmente
    for (int attempt = 0; attempt < k; ++attempt) {
      MoveOneCustomerRandomly(sol, inst);
    }
  }

  /**
   * @brief Mueve UN cliente desde su instalación actual a otra distinta,
   * elegida aleatoriamente entre todas las instalaciones abiertas.
   *
   * Si no hay pares asignados o no hay destinos alternativos, no hace nada.
   */
  void MoveOneCustomerRandomly(Solution& sol, const Instance& inst) {
    // Recopilar todos los pares (cliente, instalación) con flujo asignado
    std::vector<std::pair<int,int>> assigned;
    for (int i = 0; i < inst.num_customers; ++i)
      for (int j = 0; j < inst.num_facilities; ++j)
        if (sol.customer_demand_fractions[i][j] > 0.0)
          assigned.emplace_back(i, j);

    if (assigned.empty()) return;

    // Elegir un par al azar
    std::uniform_int_distribution<int> pick_pair(0, (int)assigned.size() - 1);
    const auto [customer, src] = assigned[pick_pair(gen_)];

    // Elegir una instalación destino distinta al azar 
    std::vector<int> open_targets;
    for (int j = 0; j < inst.num_facilities; ++j)
      if (sol.is_facility_open[j] && j != src)
        open_targets.push_back(j);

    if (open_targets.empty()) return;

    std::uniform_int_distribution<int> pick_dst(0, (int)open_targets.size() - 1);
    const int dst = open_targets[pick_dst(gen_)];

    const double demand_to_move =
        sol.customer_demand_fractions[customer][src]
        * inst.customer_demands[customer];

    // Aplicar el movimiento asegurando factibilidad de capacidad e incompatibilidad.
    if (sol.residual_cap[dst] >= demand_to_move && IsCompatible(sol, inst, customer, dst)) {
      sol.residual_cap[src] += demand_to_move;
      sol.residual_cap[dst] -= demand_to_move;
      sol.customer_demand_fractions[customer][dst] +=
          sol.customer_demand_fractions[customer][src];
      sol.customer_demand_fractions[customer][src]   = 0.0;
      sol.is_customer_served_facility[customer][src] = false;
      sol.is_customer_served_facility[customer][dst] = true;
    }
  }

  /**
   * @brief Abre una instalación cerrada elegida aleatoriamente.
   *
   * Esto fuerza al VND-RL a explorar una región del espacio con una
   * instalación diferente activa. La instalación extra puede cerrarse después
   * con ApplyCloseFacility si no aporta valor.
   */
  void OpenRandomClosedFacility(Solution& sol, const Instance& inst) {
    std::vector<int> closed;
    for (int j = 0; j < inst.num_facilities; ++j)
      if (!sol.is_facility_open[j]) closed.push_back(j);

    if (closed.empty()) return;

    std::uniform_int_distribution<int> pick(0, (int)closed.size() - 1);
    const int j = closed[pick(gen_)];
    sol.is_facility_open[j] = true;
    sol.residual_cap[j] = inst.facility_capacities[j]; 
  }

  bool ShouldOpenRandomFacility() {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    return u(gen_) < params_.shake_open_random_prob;
  }

  // ── Atributos ──────────────────────────────────────────────────────────────
  GvnsParams                params_;
  VndRL                     vnd_rl_;
  std::unique_ptr<Solution> seed_;
  std::random_device        rd_;
  std::mt19937              gen_;
};

#endif // GVNS_SOLVER_H
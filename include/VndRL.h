/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   VndRL.h
 * @author Guillermo López Concepción
 * @brief  VND con Reinforcement Learning (VND-RL).
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * Descripción
 * ──────────────────────────────────────────────────────────────────────────────
 * Sustituye el orden estático de un VND clásico por una política ε-greedy que
 * aprende qué búsqueda local (LS) es más prometedora en el estado actual.
 *
 * Regla de actualización (Q-learning simplificado):
 *   Q(LS_k) ← Q(LS_k) + α · [r − Q(LS_k)]
 *
 * donde r = 1 si LS_k mejoró la solución, r = 0 si no lo hizo.
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * CORRECCIÓN CRÍTICA respecto a versiones anteriores
 * ──────────────────────────────────────────────────────────────────────────────
 * Las búsquedas LS1 (Shift) y LS2 (SwapClients) modifican las fracciones y la
 * capacidad residual de la solución pero NO llaman a sol.Evaluate() internamente.
 * Por tanto, sol.total_cost no se actualiza tras su ejecución.
 *
 * BUG anterior: se comparaba "sol.total_cost < cost_before" SIN llamar primero
 * a Evaluate(), haciendo que la comparación fuera siempre falsa → reward = 0
 * siempre → la tabla Q aprendía incorrectamente que LS1/LS2 no sirven → el
 * VND-RL terminaba rápido sin detectar ninguna mejora real.
 *
 * CORRECCIÓN: llamar a sol.Evaluate() SIEMPRE que changed == true, antes de
 * comparar costes. Esto tiene un coste O(n·m) por iteración pero es necesario
 * para que la detección de mejora sea correcta.
 * ──────────────────────────────────────────────────────────────────────────────
 */
#ifndef VND_RL_H
#define VND_RL_H

#include "Solution.h"
#include "LocalSearch.h"
#include <array>
#include <algorithm>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
/// Parámetros del VND-RL
struct VndRLParams {
  double alpha   = 0.2;  ///< Tasa de aprendizaje α ∈ (0,1). Recomendado: 0.1–0.3
  double epsilon = 0.2;  ///< Probabilidad de exploración ε. Recomendado: 0.1–0.2
  int    max_iterations_without_improvement = 30;  ///< Parada si no hay mejora
  int    max_total_iterations               = 800; ///< Paracaídas de seguridad
  bool   use_proportional_reward            = false; ///< true → recompensa proporcional
};

// ─────────────────────────────────────────────────────────────────────────────
class VndRL {
 public:
  /// Número de búsquedas locales disponibles (LS1–LS5)
  static constexpr int kNumLS = 5;

  explicit VndRL(VndRLParams params = {})
      : params_(params), rd_(), gen_(rd_()) {
    ResetQTable();
  }

  // ── Control de la tabla Q ──────────────────────────────────────────────────

  /**
   * @brief Reinicia la tabla Q al valor neutro 0.5.
   *
   * Llamar SOLO al inicio del GVNS, no entre iteraciones, para que el
   * conocimiento acumulado se transfiera entre perturbaciones.
   */
  void ResetQTable() {
    q_table_.fill(0.5);
  }

  // ── Mejora ─────────────────────────────────────────────────────────────────

  /**
   * @brief Bucle VND-RL: mejora @p sol usando la política ε-greedy sobre Q.
   *
   * Pseudocódigo (Diapositiva 13 de las transparencias):
   *
   *   mientras sin_mejora < MAX_SIN_MEJORA AND total < MAX_TOTAL:
   *     k        = ε-greedy(Q)
   *     changed  = LS_k(s)          ← modifica s in-place
   *     si changed:
   *       Evaluate(s)               ← ¡CRÍTICO! actualiza s.total_cost
   *     r = 1 si s mejoró, 0 si no
   *     Q(k) ← Q(k) + α·[r − Q(k)]
   *     si mejoró: reiniciar contador
   *     sino:      incrementar contador
   *
   * @param sol   Solución que se mejora in-place.
   * @param inst  Instancia del problema (solo lectura).
   */
  void Improve(Solution& sol, const Instance& inst) {
    int iters_without_improvement = 0;
    int total_iters               = 0;

    while (iters_without_improvement < params_.max_iterations_without_improvement
           && total_iters < params_.max_total_iterations) {

      const int    k           = SelectLocalSearch();
      const double cost_before = sol.total_cost;

      const bool changed = ExecuteLocalSearch(k, sol, inst);

      // ── CORRECCIÓN DEL BUG: evaluar SIEMPRE antes de comparar costes ──────
      // LS1 y LS2 no llaman a Evaluate() internamente. Si no llamamos aquí,
      // sol.total_cost permanece en cost_before y la comparación siempre falla.
      if (changed) {
        sol.Evaluate(inst);
      }
      // ─────────────────────────────────────────────────────────────────────

      double reward = 0.0;

      if (changed && sol.total_cost < cost_before - 1e-4) {
        // Mejora real confirmada
        if (params_.use_proportional_reward && cost_before > 0.0)
          reward = (cost_before - sol.total_cost) / cost_before;
        else
          reward = 1.0;

        iters_without_improvement = 0;
      } else {
        reward = 0.0;
        ++iters_without_improvement;
      }

      // Actualizar tabla Q: Q ← Q + α·(r − Q)
      UpdateQValue(k, reward);
      ++total_iters;
    }
  }

 private:
  // ── Política ε-greedy ─────────────────────────────────────────────────────

  /**
   * @brief Selecciona la búsqueda local según la política ε-greedy.
   *
   * Con probabilidad ε   → exploración: índice aleatorio uniforme.
   * Con probabilidad 1-ε → explotación: LS con mayor Q.
   */
  int SelectLocalSearch() {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    if (uniform(gen_) < params_.epsilon) {
      return std::uniform_int_distribution<int>(0, kNumLS - 1)(gen_);
    }
    return static_cast<int>(
        std::max_element(q_table_.begin(), q_table_.end()) - q_table_.begin());
  }

  // ── Despacho de búsquedas locales ─────────────────────────────────────────

  /**
   * @brief Ejecuta la búsqueda local LS_k sobre @p sol.
   *
   * Nota: LS1 y LS2 no llaman a Evaluate() internamente.
   * La actualización de sol.total_cost la hace Improve() tras esta llamada.
   *
   * @return true si se realizó al menos un movimiento.
   */
  bool ExecuteLocalSearch(int k, Solution& sol, const Instance& inst) {
    switch (k) {
      case 0: return ApplyShift(sol, inst);
      case 1: return ApplySwapClients(sol, inst);
      case 2: return ApplyCloseFacility(sol, inst);
      case 3: return ApplySwapFacilities(sol, inst);
      case 4: return ApplyEliminateIncompatibility(sol, inst);
      default: return false;
    }
  }

  // ── Actualización Q ───────────────────────────────────────────────────────

  /// Aplica Q ← Q + α·(r − Q).
  void UpdateQValue(int k, double reward) {
    q_table_[k] += params_.alpha * (reward - q_table_[k]);
  }

  // ── Atributos ──────────────────────────────────────────────────────────────
  VndRLParams                params_;
  std::array<double, kNumLS> q_table_;
  std::random_device         rd_;
  std::mt19937               gen_;
};

#endif // VND_RL_H
/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   Solution.h
 * @author Guillermo López Concepción
 * @brief  Estructura que representa una solución completa del MS-CFLP-CI.
 *
 * Variables de decisión (nomenclatura del modelo matemático):
 *   y_j  → is_facility_open[j]              (binaria: instalación abierta)
 *   x_ij → customer_demand_fractions[i][j]  (real ∈ [0,1]: fracción de demanda)
 *   w_ij → is_customer_served_facility[i][j](binaria: cliente i servido por j)
 *
 * Variable auxiliar:
 *   r_j  → residual_cap[j]                  (capacidad libre de la instalación j)
 */
#ifndef SOLUTION_H
#define SOLUTION_H

#include "Instance.h"

// ─────────────────────────────────────────────────────────────────────────────
struct Solution {
  // ── Variables de decisión ──────────────────────────────────────────────────
  std::vector<bool>                is_facility_open;            ///< y_j
  std::vector<std::vector<double>> customer_demand_fractions;   ///< x_ij
  std::vector<std::vector<bool>>   is_customer_served_facility; ///< w_ij

  // ── Variable auxiliar ──────────────────────────────────────────────────────
  std::vector<double> residual_cap;  ///< r_j = s_j − Σ_i (d_i · x_ij)

  // ── Función objetivo ───────────────────────────────────────────────────────
  double total_cost = 0.0;  ///< Σ f_j·y_j  +  Σ c_ij·d_i·x_ij
  double fixed_cost = 0.0;  ///< Σ f_j·y_j
  double transport_cost = 0.0; ///< Σ c_ij·d_i·x_ij
  // ── Constructor ────────────────────────────────────────────────────────────

  /**
   * @brief Inicializa la solución a "vacía" (ninguna instalación abierta,
   *        ningún flujo asignado) con las dimensiones de la instancia dada.
   */
  explicit Solution(const Instance& inst) {
    is_facility_open.assign(inst.num_facilities, false);
    customer_demand_fractions.assign(
        inst.num_customers, std::vector<double>(inst.num_facilities, 0.0));
    is_customer_served_facility.assign(
        inst.num_customers, std::vector<bool>(inst.num_facilities, false));
    residual_cap = inst.facility_capacities;
  }

  // ── Evaluación ─────────────────────────────────────────────────────────────

  /**
   * @brief Recalcula total_cost a partir de las variables de decisión actuales.
   *
   * Coste fijo:      Σ_{j: abierta} f_j
   * Coste transporte: Σ_i Σ_j  c_ij · d_i · x_ij
   */
  void Evaluate(const Instance& inst) {
    // 1. Reiniciar a 0 usando las variables de la estructura
    fixed_cost = 0.0;
    transport_cost = 0.0;

    // 2. Calcular coste fijo
    for (int j = 0; j < inst.num_facilities; ++j) {
      if (is_facility_open[j]) {
        fixed_cost += inst.facility_fixed_costs[j];
      }
    }

    // 3. Calcular coste de transporte
    for (int i = 0; i < inst.num_customers; ++i) {
      for (int j = 0; j < inst.num_facilities; ++j) {
        transport_cost += (inst.unit_transport_costs[i][j] * inst.customer_demands[i]) 
                          * customer_demand_fractions[i][j];
      }
    }

    // 4. Actualizar el total
    total_cost = fixed_cost + transport_cost;
  }
};

#endif // SOLUTION_H

/**
 * Universidad de La Laguna
 * Escuela Superior de Ingeniería y Tecnología
 * Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos
 * Práctica 5: MS-CFLP-CI - Solvers Voraz y GRASP
 * * @file GreedySolver.h
 * @author Guillermo López Concepción
 * @brief Implementa el solver Voraz: Selección de instalaciones por coste fijo + Asignación de clientes 
 * a la instalación más barata compatible. 
 */
#ifndef GREEDY_SOLVER_H
#define GREEDY_SOLVER_H

#include "SolverTemplate.h"
#include <algorithm>
#include <numeric>
#include <vector>

/**
 * @class GreedySolver
 * @brief Traducción literal del Algorithm 1: Algoritmo voraz para el MS-CFLP-CI.
 */
class GreedySolver : public SolverTemplate {
 protected:
  /**
   * @brief Implementación fiel del pseudocódigo proporcionado.
   */
  Solution Construct(const Instance& inst) override {
    const int k = 5;
    Solution sol(inst);

    // --- FASE 1: SELECCIÓN DE INSTALACIONES ---

    // Ordenar F en orden ascendente según fj
    std::vector<int> F(inst.num_facilities);
    std::iota(F.begin(), F.end(), 0);
    
    // SOLUCIÓN AL BUG: std::stable_sort garantiza que en caso de empate 
    // se preserve el orden original. Hace el algoritmo 100% determinista.
    std::stable_sort(F.begin(), F.end(), [&](int a, int b) {
      return inst.facility_fixed_costs[a] < inst.facility_fixed_costs[b];
    });
    
    double D_total = inst.total_demand;
    double capacidad_acumulada = 0;
    // 5-8. foreach j en F...
    int last_index = 0;
    for (int idx = 0; idx < inst.num_facilities; ++idx) {
      int j = F[idx];
      if (capacidad_acumulada < D_total) {
        sol.is_facility_open[j] = true;
        capacidad_acumulada += inst.facility_capacities[j];
        last_index = idx;
      } else {
        break;
      }
    }

    // Añadir holgura por incompatibilidad 
    int extra_count = 0;
    for (int idx = last_index + 1; idx < inst.num_facilities && extra_count < k; ++idx) {
      int j = F[idx];
      if (!sol.is_facility_open[j]) {
        sol.is_facility_open[j] = true;
        extra_count++;
      }
    }

    // --- FASE 2: ASIGNACIÓN DE CLIENTES ---
    for (int i = 0; i < inst.num_customers; ++i) {
      std::vector<int> L_i;
      for (int j = 0; j < inst.num_facilities; ++j) {
        if (sol.is_facility_open[j]) L_i.push_back(j);
      }
      
      // SOLUCIÓN AL BUG: std::stable_sort para desempatar transportes idénticos
      std::stable_sort(L_i.begin(), L_i.end(), [&](int a, int b) {
        return inst.unit_transport_costs[i][a] < inst.unit_transport_costs[i][b];
      });

      double d_i_restante = inst.customer_demands[i]; // Copia de la demanda para el bucle while

      for (size_t idx_L = 0; idx_L < L_i.size() && d_i_restante > 0; ++idx_L) {
        int j = L_i[idx_L];

        if (IsCompatible(sol, inst, i, j)) {
          double q = std::min(d_i_restante, sol.residual_cap[j]);

          if (q > 0) {
            sol.customer_demand_fractions[i][j] += (q / inst.customer_demands[i]);
            d_i_restante -= q;
            sol.residual_cap[j] -= q;
            sol.is_customer_served_facility[i][j] = true;
          }
        }
      }
    }

    // Calcular el coste de la solución construida, S
    sol.Evaluate(inst);
    return sol;
  }
};

#endif
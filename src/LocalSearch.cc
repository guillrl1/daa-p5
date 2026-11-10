/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   LocalSearch.cc
 * @author Guillermo López Concepción
 * @brief  Implementación de las cuatro búsquedas locales del MS-CFLP-CI y de
 *         las utilidades de factibilidad compartidas.
 */
#include "../include/LocalSearch.h"
#include <algorithm>
#include <numeric>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
// Constantes internas
// ─────────────────────────────────────────────────────────────────────────────
namespace {

/// Umbral de mejora mínima para evitar bucles por errores de punto flotante.
constexpr double kEpsilon = 1e-4;

/**
 * @brief Calcula el delta de coste de transporte al intercambiar los clientes
 *        i1 e i2 entre sus respectivas instalaciones j1 y j2.
 *
 * delta > 0  ⟹ el intercambio empeora el coste.
 * delta < 0  ⟹ el intercambio mejora el coste.
 */
double SwapClientsCostDelta(const Instance& inst,
                            int i1, int i2, int j1, int j2) {
  const double before = inst.unit_transport_costs[i1][j1] * inst.customer_demands[i1]
                      + inst.unit_transport_costs[i2][j2] * inst.customer_demands[i2];
  const double after  = inst.unit_transport_costs[i1][j2] * inst.customer_demands[i1]
                      + inst.unit_transport_costs[i2][j1] * inst.customer_demands[i2];
  return after - before;
}

} // namespace anónimo

// ─────────────────────────────────────────────────────────────────────────────
// Utilidades de factibilidad
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Verifica si el cliente @p customer es compatible con la instalación @p facility
 * @param sol Solución actual
 * @param inst Instancia del problema
 * @param customer Índice del cliente
 * @param facility Índice de la instalación
 * @return true si el cliente es compatible con la instalación, false en caso contrario
 */
bool IsCompatible(const Solution& sol, const Instance& inst,
                  int customer, int facility) {
  // Un cliente es incompatible si alguno de sus «enemigos» ya está en @p facility
  for (int incompatible : inst.incompatibilities[customer]) {
    if (sol.is_customer_served_facility[incompatible][facility]) return false;
  }
  return true;
}

/**
 * @brief Asigna los flujos de clientes a instalaciones abiertas de forma voraz por coste unitario.
 */
void AssignFlows(Solution& sol, const Instance& inst) {
  // Reiniciar todas las asignaciones y recuperar capacidades completas
  for (int i = 0; i < inst.num_customers; ++i)
    for (int j = 0; j < inst.num_facilities; ++j) {
      sol.customer_demand_fractions[i][j]   = 0.0;
      sol.is_customer_served_facility[i][j] = false;
    }
  sol.residual_cap = inst.facility_capacities;

  // Para cada cliente, asignar su demanda de forma voraz por coste ascendente
  for (int i = 0; i < inst.num_customers; ++i) {
    // Recopilar instalaciones abiertas y ordenarlas por coste unitario para i
    std::vector<int> open_facilities;
    open_facilities.reserve(inst.num_facilities);
    for (int j = 0; j < inst.num_facilities; ++j)
      if (sol.is_facility_open[j]) open_facilities.push_back(j);

    std::sort(open_facilities.begin(), open_facilities.end(), [&](int a, int b) {
      return inst.unit_transport_costs[i][a] < inst.unit_transport_costs[i][b];
    });

    double remaining = inst.customer_demands[i];

    for (int j : open_facilities) {
      if (remaining <= 0.0) break;
      if (!IsCompatible(sol, inst, i, j)) continue;

      const double assigned = std::min(remaining, sol.residual_cap[j]);
      if (assigned <= 0.0) continue;

      sol.customer_demand_fractions[i][j]   = assigned / inst.customer_demands[i];
      sol.is_customer_served_facility[i][j] = true;
      sol.residual_cap[j]  -= assigned;
      remaining            -= assigned;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// LS1 – Shift (reinserción)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief LS1 – Shift (reinserción).
 * Intenta mover la asignación completa de un cliente desde su instalación actual a otra distinta.
 */
bool ApplyShift(Solution& sol, const Instance& inst) {
  for (int i = 0; i < inst.num_customers; ++i) {
    for (int src = 0; src < inst.num_facilities; ++src) {
      if (sol.customer_demand_fractions[i][src] <= 0.0) continue;

      const double demand_to_move =
          sol.customer_demand_fractions[i][src] * inst.customer_demands[i];

      for (int dst = 0; dst < inst.num_facilities; ++dst) {
        if (!sol.is_facility_open[dst] || dst == src) continue;
        if (sol.residual_cap[dst] < demand_to_move)   continue;

        // Solo aceptar movimientos estrictamente mejorantes
        const double delta = (inst.unit_transport_costs[i][dst]
                            - inst.unit_transport_costs[i][src]) * demand_to_move;
        if (delta >= -kEpsilon) continue;

        if (!IsCompatible(sol, inst, i, dst)) continue;

        // Aplicar el movimiento
        sol.residual_cap[src] += demand_to_move;
        sol.residual_cap[dst] -= demand_to_move;
        sol.customer_demand_fractions[i][dst] +=
            sol.customer_demand_fractions[i][src];
        sol.customer_demand_fractions[i][src]   = 0.0;
        sol.is_customer_served_facility[i][src] = false;
        sol.is_customer_served_facility[i][dst] = true;
        return true;
      }
    }
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LS2 – Swap-Clientes (intercambio de asignaciones)
// ─────────────────────────────────────────────────────────────────────────────
/**
 * @brief LS2 – Swap-Clientes (intercambio de asignaciones).
 *
 * Considera todos los pares de clientes (i1, i2) en los que cada uno está
 * completamente asignado a una única instalación (fracción == 1).
 * Intercambia sus instalaciones si: el coste total baja, la capacidad lo
 * permite y no se generan incompatibilidades cruzadas.
 *
 * @return true si se realizó algún movimiento.
 */
bool ApplySwapClients(Solution& sol, const Instance& inst) {
  for (int i1 = 0; i1 < inst.num_customers; ++i1) {
    for (int i2 = i1 + 1; i2 < inst.num_customers; ++i2) {
      for (int j1 = 0; j1 < inst.num_facilities; ++j1) {
        // i1 debe estar completamente en j1
        if (sol.customer_demand_fractions[i1][j1] < 0.999) continue;

        for (int j2 = 0; j2 < inst.num_facilities; ++j2) {
          // i2 debe estar completamente en j2, y j1 ≠ j2
          if (sol.customer_demand_fractions[i2][j2] < 0.999) continue;
          if (j1 == j2) continue;

          // Solo intercambios mejorantes
          if (SwapClientsCostDelta(inst, i1, i2, j1, j2) >= -kEpsilon) continue;

          const double d1 = inst.customer_demands[i1];
          const double d2 = inst.customer_demands[i2];

          // Comprobar que la capacidad residual lo permite tras el intercambio
          const bool capacity_ok = (sol.residual_cap[j1] + d1 >= d2)
                                && (sol.residual_cap[j2] + d2 >= d1);
          if (!capacity_ok) continue;

          // Comprobar compatibilidades cruzadas
          if (!IsCompatible(sol, inst, i1, j2)) continue;
          if (!IsCompatible(sol, inst, i2, j1)) continue;

          // Aplicar el intercambio
          sol.customer_demand_fractions[i1][j1]   = 0.0;
          sol.is_customer_served_facility[i1][j1] = false;
          sol.customer_demand_fractions[i2][j2]   = 0.0;
          sol.is_customer_served_facility[i2][j2] = false;

          sol.customer_demand_fractions[i1][j2]   = 1.0;
          sol.is_customer_served_facility[i1][j2] = true;
          sol.customer_demand_fractions[i2][j1]   = 1.0;
          sol.is_customer_served_facility[i2][j1] = true;

          sol.residual_cap[j1] += d1 - d2;
          sol.residual_cap[j2] += d2 - d1;
          return true;
        }
      }
    }
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LS3 – Swap-Instalaciones (cierre/apertura de instalación)
// ─────────────────────────────────────────────────────────────────────────────
/**
 * @brief LS3 – Swap-Instalaciones (cierre/apertura de instalación).
 * Considera todos los pares de instalaciones (j1, j2) en los que j1 está abierta
 * y j2 cerrada. Intenta cerrar j1 y abrir j2, reasignando todos los clientes con el método voraz. Si el coste total baja y no
 * se pierde factibilidad, se acepta el movimiento.
 */

bool ApplySwapFacilities(Solution& sol, const Instance& inst) {
  for (int open_fac = 0; open_fac < inst.num_facilities; ++open_fac) {
    if (!sol.is_facility_open[open_fac]) continue;

    for (int closed_fac = 0; closed_fac < inst.num_facilities; ++closed_fac) {
      if (sol.is_facility_open[closed_fac]) continue;

      // Construir solución candidata con el intercambio
      Solution candidate = sol;
      candidate.is_facility_open[open_fac]    = false;
      candidate.is_facility_open[closed_fac]  = true;

      AssignFlows(candidate, inst);
      candidate.Evaluate(inst);

      // Verificar que todos los clientes quedaron servidos completamente
      bool all_served = true;
      for (int i = 0; i < inst.num_customers && all_served; ++i) {
        double total_fraction = 0.0;
        for (int j = 0; j < inst.num_facilities; ++j)
          total_fraction += candidate.customer_demand_fractions[i][j];
        if (total_fraction < 0.999) all_served = false;
      }
      if (!all_served) continue;

      if (candidate.total_cost < sol.total_cost - kEpsilon) {
        sol = candidate;
        return true;
      }
    }
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LS4 – Eliminación de incompatibilidad
// ─────────────────────────────────────────────────────────────────────────────
/**
 * @brief LS4 – Eliminación de incompatibilidad.
 *
 * Identifica la instalación con mayor coste de transporte. Dentro de esa
 * instalación, el cliente que más bloqueos genera a otros clientes en otras
 * instalaciones abiertas. Intenta mover ese cliente a una instalación más barata
 * y compatible. Si el coste total baja, se acepta el movimiento.
 *
 * @return true si se realizó algún movimiento.
 */
bool ApplyEliminateIncompatibility(Solution& sol, const Instance& inst) {
  // 1. Identificar la instalación con mayor coste total de transporte
  int    worst_facility      = -1;
  double max_transport_cost  = -1.0;

  for (int j = 0; j < inst.num_facilities; ++j) {
    if (!sol.is_facility_open[j]) continue;
    double cost = 0.0;
    for (int i = 0; i < inst.num_customers; ++i)
      cost += inst.unit_transport_costs[i][j]
            * inst.customer_demands[i]
            * sol.customer_demand_fractions[i][j];
    if (cost > max_transport_cost) {
      max_transport_cost = cost;
      worst_facility     = j;
    }
  }
  if (worst_facility == -1) return false;

  // 2. Dentro de esa instalación, el cliente que más bloqueos genera
  int blocking_customer   = -1;
  int max_blocking_count  = -1;

  for (int i = 0; i < inst.num_customers; ++i) {
    if (!sol.is_customer_served_facility[i][worst_facility]) continue;
    // Contar incompatibilidades activas de i en otras instalaciones abiertas
    int count = 0;
    for (int incompatible : inst.incompatibilities[i])
      for (int j = 0; j < inst.num_facilities; ++j)
        if (j != worst_facility && sol.is_customer_served_facility[incompatible][j])
          ++count;

    if (count > max_blocking_count) {
      max_blocking_count = count;
      blocking_customer  = i;
    }
  }
  if (blocking_customer == -1) return false;

  // 3. Intentar mover ese cliente a una instalación más barata y compatible
  const double demand_to_move =
      sol.customer_demand_fractions[blocking_customer][worst_facility]
      * inst.customer_demands[blocking_customer];

  for (int dst = 0; dst < inst.num_facilities; ++dst) {
    if (!sol.is_facility_open[dst] || dst == worst_facility)     continue;
    if (sol.residual_cap[dst] < demand_to_move)                  continue;
    if (!IsCompatible(sol, inst, blocking_customer, dst))        continue;

    const double delta =
        (inst.unit_transport_costs[blocking_customer][dst]
       - inst.unit_transport_costs[blocking_customer][worst_facility])
        * demand_to_move;

    if (delta >= -kEpsilon) continue;  // solo movimientos mejorantes

    // Aplicar el movimiento
    sol.residual_cap[worst_facility] += demand_to_move;
    sol.residual_cap[dst]            -= demand_to_move;
    sol.customer_demand_fractions[blocking_customer][dst] +=
        sol.customer_demand_fractions[blocking_customer][worst_facility];
    sol.customer_demand_fractions[blocking_customer][worst_facility]   = 0.0;
    sol.is_customer_served_facility[blocking_customer][worst_facility] = false;
    sol.is_customer_served_facility[blocking_customer][dst]            = true;
    sol.Evaluate(inst);
    return true;
  }
  return false;
}

// ─── LS5: Cierre de Instalación (Drop Facility) ──────────────────────────────
/**
 * @brief LS5 – Cierre de Instalación (Drop Facility).
 * Considera cada instalación abierta y evalúa el impacto de cerrarla completamente.
 * Para cada instalación candidata, se reasignan los clientes afectados usando el método voraz.
 * Si el coste total baja y no se pierde factibilidad, se acepta el movimiento.
 *
 * @return true si se realizó algún movimiento.
 */
bool ApplyCloseFacility(Solution& sol, const Instance& inst) {
  for (int j = 0; j < inst.num_facilities; ++j) {
    // Solo intentamos cerrar instalaciones que estén abiertas
    if (!sol.is_facility_open[j]) continue;

    // Crear una solución candidata con esta instalación cerrada
    Solution candidate = sol;
    candidate.is_facility_open[j] = false;

    // Reasignar flujos usando el método voraz que ya tienes
    AssignFlows(candidate, inst);

    // Verificar si todos los clientes pudieron ser reubicados (factibilidad)
    bool all_served = true;
    for (int i = 0; i < inst.num_customers; ++i) {
      double total_fraction = 0.0;
      for (int f = 0; f < inst.num_facilities; ++f) {
        total_fraction += candidate.customer_demand_fractions[i][f];
      }
      // Si algún cliente no llegó al 100% de su demanda, no es factible
      if (total_fraction < 0.999) {
        all_served = false;
        break;
      }
    }

    if (!all_served) continue;

    // Si es factible, evaluamos si sale más barato
    candidate.Evaluate(inst);
    if (candidate.total_cost < sol.total_cost - kEpsilon) {
      sol = candidate;
      return true;
    }
  }
  return false;
}

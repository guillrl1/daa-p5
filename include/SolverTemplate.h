/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   SolverTemplate.h
 * @author Guillermo López Concepción
 * @brief  Clase base abstracta que implementa el patrón «Template Method»
 *         para todos los solvers del MS-CFLP-CI.
 *
 * Esqueleto del algoritmo definido en Solve():
 *   1. Construct()     – fase constructiva (obligatoria en cada subclase).
 *   2. LocalSearch()   – fase de mejora   (opcional; no-op por defecto).
 *   3. Evaluate()      – calcula el coste final.
 *
 * Las utilidades IsCompatible() y AssignFlows() están declaradas en
 * LocalSearch.h para que todos los solvers compartan exactamente la misma
 * lógica de verificación de factibilidad.
 */
#ifndef SOLVER_TEMPLATE_H
#define SOLVER_TEMPLATE_H

#include "Solution.h"
#include "LocalSearch.h"

// ─────────────────────────────────────────────────────────────────────────────
class SolverTemplate {
 public:
  virtual ~SolverTemplate() = default;

  /**
   * @brief Ejecuta el algoritmo completo y devuelve la mejor solución.
   * @param inst  Instancia del problema (solo lectura).
   */
  Solution Solve(const Instance& inst) {
    Solution sol = Construct(inst);
    LocalSearch(sol, inst);
    sol.Evaluate(inst);
    return sol;
  }

 protected:
  /// Fase constructiva obligatoria — cada solver concreto la implementa.
  virtual Solution Construct(const Instance& inst) = 0;

  /// Fase de mejora opcional — no hace nada por defecto.
  virtual void LocalSearch(Solution&, const Instance&) {}
};

#endif // SOLVER_TEMPLATE_H

/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   LocalSearch.h
 * @author Guillermo López Concepción
 * @brief  Declara las cuatro búsquedas locales del MS-CFLP-CI y las utilidades
 *         de factibilidad compartidas por todos los solvers.
 *
 * Las cuatro búsquedas (LS1–LS4) implementan el principio «first improvement»:
 * cada función aplica el PRIMER movimiento que mejore el coste y devuelve true.
 * Si no existe ningún movimiento de mejora devuelve false (óptimo local).
 *
 *   LS1 – ApplyShift                    : reinserción de la demanda de un cliente
 *   LS2 – ApplySwapClients              : intercambio de asignaciones entre dos clientes
 *   LS3 – ApplySwapFacilities           : cierre de una instalación / apertura de otra
 *   LS4 – ApplyEliminateIncompatibility : mueve al cliente más bloqueante
 */
#ifndef LOCAL_SEARCH_H
#define LOCAL_SEARCH_H

#include "Solution.h"

// ─── Búsquedas locales ────────────────────────────────────────────────────────

/**
 * @brief LS1 – Shift (reinserción).
 *
 * Para cada par (cliente i, instalación origen j1) con flujo asignado, busca
 * una instalación destino j2 con menor coste unitario que tenga capacidad
 * residual suficiente y sea compatible con i.  Aplica el primer movimiento
 * estrictamente mejorante (coste_delta < -ε).
 *
 * Complejidad: O(n · m²) por llamada.
 * @return true si se realizó algún movimiento.
 */
bool ApplyShift(Solution& sol, const Instance& inst);

/**
 * @brief LS2 – Swap-Clientes (intercambio de asignaciones).
 *
 * Considera todos los pares de clientes (i1, i2) en los que cada uno está
 * completamente asignado a una única instalación (fracción == 1).
 * Intercambia sus instalaciones si: el coste total baja, la capacidad lo
 * permite y no se generan incompatibilidades cruzadas.
 *
 * Complejidad: O(n² · m²) por llamada.
 * @return true si se realizó algún movimiento.
 */
bool ApplySwapClients(Solution& sol, const Instance& inst);

/**
 * @brief LS3 – Swap-Instalaciones (cierre/apertura de instalación).
 *
 * Prueba cerrar cada instalación abierta y abrir cada instalación cerrada.
 * Tras el intercambio reasigna todos los flujos con AssignFlows() y acepta
 * únicamente si el coste total mejora y todos los clientes quedan servidos.
 *
 * Complejidad: O(m² · (n·m)) por llamada (costosa, pero llamada con moderación).
 * @return true si se realizó algún movimiento.
 */
bool ApplySwapFacilities(Solution& sol, const Instance& inst);

/**
 * @brief LS4 – Eliminación de incompatibilidad.
 *
 * Identifica la instalación con mayor coste total de transporte («peor
 * instalación») y dentro de ella al cliente que bloquea más asignaciones
 * por incompatibilidad.  Intenta mover ese cliente a otra instalación abierta
 * de menor coste, aceptando solo si el movimiento es mejorante.
 *
 * Complejidad: O(m · n · m) por llamada.
 * @return true si se realizó algún movimiento.
 */
bool ApplyEliminateIncompatibility(Solution& sol, const Instance& inst);

// ─── Utilidades de factibilidad ───────────────────────────────────────────────

/**
 * @brief Comprueba si el cliente @p customer puede asignarse a @p facility
 *        sin violar ninguna restricción de incompatibilidad con los clientes
 *        que ya están siendo servidos por esa instalación.
 *
 * @return true si la asignación es compatible (factible).
 */
bool IsCompatible(const Solution& sol, const Instance& inst,
                  int customer, int facility);

/**
 * @brief Reasigna todos los flujos de las instalaciones abiertas desde cero.
 *
 * Para cada cliente ordena las instalaciones abiertas por coste unitario
 * ascendente y asigna su demanda de forma voraz respetando capacidades e
 * incompatibilidades.  Actualiza customer_demand_fractions,
 * is_customer_served_facility y residual_cap.
 *
 * Se usa al inicializar soluciones y tras movimientos que cambian qué
 * instalaciones están abiertas (LS3).
 */
void AssignFlows(Solution& sol, const Instance& inst);

/**
 * @brief LS5 – Cierre de Instalación: intenta cerrar una instalación abierta y 
 * reasignar todos sus clientes a las instalaciones que quedan abiertas.
 * Si es factible y ahorra el coste fijo, se acepta.
 * @return true si se realizó algún movimiento de mejora.
 */
bool ApplyCloseFacility(Solution& sol, const Instance& inst);

#endif // LOCAL_SEARCH_H

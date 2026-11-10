/**
 * Universidad de La Laguna – Grado en Ingeniería Informática
 * Asignatura: Diseño y Análisis de Algoritmos · Práctica 5: MS-CFLP-CI
 *
 * @file   Instance.h
 * @author Guillermo López Concepción
 * @brief  Carga y almacena los datos de una instancia del MS-CFLP-CI.
 *
 * El fichero de entrada tiene el formato .dzn estándar con los campos:
 *   nWarehouses, nStores, Capacity[], FixedCost[], Demand[],
 *   TransportCost[|...|], nIncompatibilities, Incompatibilities[|...|]
 */
#ifndef INSTANCE_H
#define INSTANCE_H

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
class Instance {
 public:
  // ── Dimensiones ────────────────────────────────────────────────────────────
  int num_customers;   ///< Número de clientes  (n)
  int num_facilities;  ///< Número de instalaciones (m)

  // ── Datos del problema ─────────────────────────────────────────────────────
  std::vector<double> customer_demands;         ///< d_i  – demanda de cada cliente
  std::vector<double> facility_capacities;      ///< s_j  – capacidad de cada instalación
  std::vector<double> facility_fixed_costs;     ///< f_j  – coste fijo de apertura

  /// c_ij – coste unitario de transporte (indexado [cliente][instalación])
  std::vector<std::vector<double>> unit_transport_costs;

  /// Para cada cliente, lista de clientes con los que es incompatible
  std::vector<std::vector<int>> incompatibilities;

  double total_demand = 0.0;  ///< Suma de todas las demandas (precalculado)

  // ── E/S ────────────────────────────────────────────────────────────────────

  /**
   * @brief Lee la instancia desde stdin en formato .dzn.
   *
   * Se apoya en CleanDelimiters() para normalizar los separadores del formato
   * MiniZinc (corchetes, barras, comas, punto y coma) antes de parsear.
   */
  void LoadFromStdin() {
    std::string label, equal;

    // Cabecera: número de instalaciones y clientes
    std::cin >> label >> equal >> num_facilities >> label;
    std::cin >> label >> equal >> num_customers  >> label;

    // Función auxiliar lambda para leer un vector de doubles hasta ';'
    auto ReadVector = [&](std::vector<double>& vec) {
      std::string buf;
      std::getline(std::cin, buf, ';');
      std::stringstream ss(CleanDelimiters(buf));
      double v;
      while (ss >> v) vec.push_back(v);
    };

    std::cin >> label >> equal; ReadVector(facility_capacities);
    std::cin >> label >> equal; ReadVector(facility_fixed_costs);
    std::cin >> label >> equal; ReadVector(customer_demands);

    total_demand = 0.0;
    for (double d : customer_demands) total_demand += d;

    // Matriz de costes de transporte (num_customers × num_facilities)
    unit_transport_costs.assign(num_customers,
                                std::vector<double>(num_facilities));
    std::cin >> label >> equal;
    std::string mat_buf;
    std::getline(std::cin, mat_buf, ';');
    std::stringstream ss_mat(CleanDelimiters(mat_buf));
    for (int i = 0; i < num_customers; ++i)
      for (int j = 0; j < num_facilities; ++j)
        ss_mat >> unit_transport_costs[i][j];

    // Pares de incompatibilidad (grafo no dirigido → almacenamos ambos sentidos)
    int num_incompatibilities;
    std::cin >> label >> equal >> num_incompatibilities >> label;
    incompatibilities.assign(num_customers, std::vector<int>());
    std::cin >> label >> equal;
    std::string inc_buf;
    std::getline(std::cin, inc_buf, ';');
    std::stringstream ss_inc(CleanDelimiters(inc_buf));
    int u, v;
    while (ss_inc >> u >> v) {
      incompatibilities[u - 1].push_back(v - 1);  // índices 1-based → 0-based
      incompatibilities[v - 1].push_back(u - 1);
    }
  }

 private:
  /// Sustituye los delimitadores del formato .dzn por espacios en blanco.
  static std::string CleanDelimiters(std::string s) {
    for (char& ch : s)
      if (std::string("[]|;,").find(ch) != std::string::npos) ch = ' ';
    return s;
  }
};

#endif // INSTANCE_H

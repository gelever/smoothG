/*BHEADER**********************************************************************
 *
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * LLNL-CODE-759464. All Rights reserved. See file COPYRIGHT for details.
 *
 * This file is part of GAUSS. For more information and source code
 * availability, see https://www.github.com/gelever/GAUSS.
 *
 * GAUSS is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 ***********************************************************************EHEADER*/

/** @file

    @brief Graph class
*/

#ifndef __GRAPH_HPP__
#define __GRAPH_HPP__

#include "Utilities.hpp"

namespace gauss
{

/**
    @brief Container for topological information for the coarsening

    Extract the local submatrix of the global vertex to edge relation table
    Each vertex belongs to one and only one processor, while some edges are
    shared by two processors, indicated by the edge to true edge relationship
*/

struct Graph
{
    /** @brief Default Constructor */
    Graph() = default;

    /**
       @brief Distribute a graph to the communicator.

       Generally we read a global graph on one processor, and then distribute
       it. This constructor handles that process.

       @param comm the communicator over which to distribute the graph
       @param vertex_edge_global describes the entire global graph, unsigned
       @param part_global partition of the global vertices
    */
    Graph(MPI_Comm comm, const SparseMatrix& vertex_edge_global,
          const std::vector<int>& part_global,
          const std::vector<double>& weight_global = {},
          const SparseMatrix& W_block_global = {});

    /**
       @brief Accepts an already distributed graph.
              Computes vertex and edge maps from local info,
              these are necessarily the same as the original maps!

       @param vertex_edge_local local vertex edge relationship
       @param edge_true_edge edge to true edge relationship
       @param part_local partition of the local vertices
    */
    Graph(SparseMatrix vertex_edge_local, ParMatrix edge_true_edge,
          std::vector<int> part_local,
          std::vector<double> weight_local = {},
          SparseMatrix W_block_local = {});

    /** @brief Default Destructor */
    ~Graph() noexcept = default;

    /** @brief Copy Constructor */
    Graph(const Graph& other) noexcept;

    /** @brief Move Constructor */
    Graph(Graph&& other) noexcept;

    /** @brief Assignment Operator */
    Graph& operator=(Graph other) noexcept;

    /** @brief Swap two graphs */
    friend void swap(Graph& lhs, Graph& rhs) noexcept;

    // Local to global maps
    std::vector<int> vertex_map_;

    // Local partition of vertices
    std::vector<int> part_local_;

    // Graph relationships
    SparseMatrix vertex_edge_local_;
    ParMatrix edge_true_edge_;
    ParMatrix edge_edge_;

    // I'm not sure this is `graph` information?
    // Graph Weight / Block
    std::vector<double> weight_local_;
    SparseMatrix W_local_;

    int global_vertices_;
    int global_edges_;

private:
    void MakeLocalWeight(const std::vector<int>& edge_map = {}, const std::vector<double>& global_weight
                         = {});

    void MakeLocalW(const SparseMatrix& W_global);
};

template <typename T>
T GetVertexVector(const Graph& graph, const T& global_vect)
{
    return GetSubVector(global_vect, graph.vertex_map_);
}

template <typename T>
void WriteVertexVector(const Graph& graph, const T& vect, const std::string& filename)
{
    WriteVector(graph.edge_true_edge_.GetComm(), vect, filename,
                graph.global_vertices_, graph.vertex_map_);
}

inline
Vector ReadVertexVector(const Graph& graph, const std::string& filename)
{
    return ReadVector(filename, graph.vertex_map_);
}

inline
BlockVector ReadVertexBlockVector(const Graph& graph, const std::string& filename)
{
    int num_vertices = graph.vertex_edge_local_.Rows();
    int num_edges = graph.vertex_edge_local_.Cols();

    BlockVector vect({0, num_edges, num_edges + num_vertices});

    vect.GetBlock(0) = 0.0;
    vect.GetBlock(1) = ReadVertexVector(graph, filename);

    return vect;
}


} // namespace gauss

#endif /* __GRAPH_HPP__ */

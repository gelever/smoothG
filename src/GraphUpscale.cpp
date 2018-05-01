/*BHEADER**********************************************************************
 *
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * LLNL-CODE-745247. All Rights reserved. See file COPYRIGHT for details.
 *
 * This file is part of smoothG. For more information and source code
 * availability, see https://www.github.com/llnl/smoothG.
 *
 * smoothG is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 ***********************************************************************EHEADER*/

/** @file

    @brief Contains GraphUpscale class
*/

#include "GraphUpscale.hpp"

namespace smoothg
{

GraphUpscale::GraphUpscale(MPI_Comm comm,
                           const linalgcpp::SparseMatrix<double>& vertex_edge_global,
                           const std::vector<int>& partitioning_global,
                           double spect_tol, int max_evects, bool hybridization,
                           const std::vector<double>& weight_global)
    : Upscale(comm, vertex_edge_global, hybridization),
      spect_tol_(spect_tol), max_evects_(max_evects)
{
    Timer timer(Timer::Start::True);

    Init(vertex_edge_global, partitioning_global, weight_global);

    timer.Click();
    setup_time_ += timer.TotalTime();
}

GraphUpscale::GraphUpscale(MPI_Comm comm,
                           const SparseMatrix& vertex_edge_global,
                           double coarse_factor,
                           double spect_tol, int max_evects, bool hybridization,
                           const std::vector<double>& weight_global)
    : Upscale(comm, vertex_edge_global, hybridization),
      spect_tol_(spect_tol), max_evects_(max_evects)
{
    Timer timer(Timer::Start::True);

    SparseMatrix edge_vertex = vertex_edge_global.Transpose();
    SparseMatrix vertex_vertex = vertex_edge_global.Mult(edge_vertex);

    int num_parts = std::max(1.0, (global_vertices_ / (double)(coarse_factor)) + 0.5);

    double ubal = 2.0;
    std::vector<int> partitioning_global = Partition(vertex_vertex, num_parts, ubal);

    Init(vertex_edge_global, partitioning_global, weight_global);

    timer.Click();
    setup_time_ += timer.TotalTime();
}

void GraphUpscale::Init(const SparseMatrix& vertex_edge,
                        const std::vector<int>& global_partitioning,
                        const std::vector<double>& weight)
{
    graph_ = Graph(comm_, vertex_edge, global_partitioning);
    gt_ = GraphTopology(graph_);

    auto mm = make_unique<ElemMixedMatrix<std::vector<double>>>(graph_, weight);
    mm->AssembleM(); // Coarsening requires assembled M, for now
    mgl_.push_back(std::move(mm));

    coarsener_ = GraphCoarsen(GetFineMatrix(), gt_,
                              max_evects_, spect_tol_);

    mgl_.push_back(coarsener_.Coarsen2(gt_, GetFineMatrix()));

    Operator::rows_ = graph_.vertex_edge_local_.Rows();
    Operator::cols_ = graph_.vertex_edge_local_.Rows();

    MakeCoarseVectors();
    MakeCoarseSolver();
    MakeFineSolver(); // TODO(gelever1): unset and let user make
}

void GraphUpscale::MakeCoarseSolver()
{
    auto& mm = dynamic_cast<ElemMixedMatrix<DenseMatrix>&>(GetCoarseMatrix());

    if (hybridization_)
    {
        coarse_solver_ = make_unique<HybridSolver>(mm, coarsener_);
    }
    else
    {
        mm.AssembleM();
        coarse_solver_ = make_unique<MinresBlockSolver>(mm);
    }
}

void GraphUpscale::MakeFineSolver()
{
    auto& mm = dynamic_cast<ElemMixedMatrix<std::vector<double>>&>(GetFineMatrix());

    if (hybridization_)
    {
        fine_solver_ = make_unique<HybridSolver>(mm);
    }
    else
    {
        mm.AssembleM();
        fine_solver_ = make_unique<MinresBlockSolver>(mm);
    }
}

Vector GraphUpscale::ReadVertexVector(const std::string& filename) const
{
    return ReadVector(filename, graph_.vertex_map_);
}

Vector GraphUpscale::ReadEdgeVector(const std::string& filename) const
{
    return ReadVector(filename, graph_.edge_map_);
}

BlockVector GraphUpscale::ReadVertexBlockVector(const std::string& filename) const
{
    BlockVector vect = GetFineBlockVector();

    vect.GetBlock(0) = 0.0;
    vect.GetBlock(1) = ReadVertexVector(filename);

    return vect;
}

BlockVector GraphUpscale::ReadEdgeBlockVector(const std::string& filename) const
{
    BlockVector vect = GetFineBlockVector();

    vect.GetBlock(0) = ReadEdgeVector(filename);
    vect.GetBlock(1) = 0.0;

    return vect;
}

void GraphUpscale::WriteVertexVector(const VectorView& vect, const std::string& filename) const
{
    WriteVector(comm_, vect, filename, global_vertices_, graph_.vertex_map_);
}

void GraphUpscale::WriteEdgeVector(const VectorView& vect, const std::string& filename) const
{
    WriteVector(comm_, vect, filename, global_edges_, graph_.edge_map_);
}

} // namespace smoothg

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

/**
   @example
   @file generalgraph.cpp
   @brief Compares a graph upscaled solution to the fine solution.
*/

#include <fstream>
#include <sstream>
#include <mpi.h>

#include "smoothG.hpp"

using namespace smoothg;

using linalgcpp::ReadText;
using linalgcpp::WriteText;
using linalgcpp::ReadCSR;

using parlinalgcpp::LOBPCG;
using parlinalgcpp::BoomerAMG;

std::vector<int> MetisPart(const SparseMatrix& vertex_edge, int num_parts);
Vector ComputeFiedlerVector(const MixedMatrix& mgl);

int main(int argc, char* argv[])
{
    // Initialize MPI
    int num_procs, myid;
    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_size(comm, &num_procs);
    MPI_Comm_rank(comm, &myid);

    // program options from command line
    std::string graph_filename = "../../graphdata/vertex_edge_sample.txt";
    std::string fiedler_filename = "../../graphdata/fiedler_sample.txt";
    std::string partition_filename = "../../graphdata/partition_sample.txt";
    std::string weight_filename = "";
    std::string w_block_filename = "";

    int isolate = -1;
    int max_evects = 4;
    double spect_tol = 1e-3;
    int num_partitions = 12;
    bool hybridization = false;
    bool metis_agglomeration = false;

    bool generate_fiedler = false;
    bool save_fiedler = false;

    bool generate_graph = false;
    int gen_vertices = 1000;
    int mean_degree = 40;
    double beta = 0.15;
    int seed = 0;

    linalgcpp::ArgParser arg_parser(argc, argv);

    arg_parser.Parse(graph_filename, "-g", "Graph connection data.");
    arg_parser.Parse(fiedler_filename, "-f", "Fiedler vector data.");
    arg_parser.Parse(partition_filename, "-p", "Partition data.");
    arg_parser.Parse(weight_filename, "-w", "Edge weight data.");
    arg_parser.Parse(w_block_filename, "-wb", "W block data.");
    arg_parser.Parse(isolate, "-isolate", "Isolate a single vertex.");
    arg_parser.Parse(max_evects, "-m", "Maximum eigenvectors per aggregate.");
    arg_parser.Parse(spect_tol, "-t", "Spectral tolerance for eigenvalue problem.");
    arg_parser.Parse(num_partitions, "-np", "Number of partitions to generate.");
    arg_parser.Parse(hybridization, "-hb", "Enable hybridization.");
    arg_parser.Parse(metis_agglomeration, "-ma", "Enable Metis partitioning.");
    arg_parser.Parse(generate_fiedler, "-gf", "Generate Fiedler vector.");
    arg_parser.Parse(save_fiedler, "-sf", "Save a generated Fiedler vector.");
    arg_parser.Parse(generate_graph, "-gg", "Generate a graph.");
    arg_parser.Parse(gen_vertices, "-nv", "Number of vertices of generated graph.");
    arg_parser.Parse(mean_degree, "-md", "Average vertex degree of generated graph.");
    arg_parser.Parse(beta, "-b", "Probability of rewiring in the Watts-Strogatz model.");
    arg_parser.Parse(seed, "-s", "Seed for random number generator.");

    if (!arg_parser.IsGood())
    {
        ParPrint(myid, arg_parser.ShowHelp());
        ParPrint(myid, arg_parser.ShowErrors());

        return EXIT_FAILURE;
    }

    ParPrint(myid, arg_parser.ShowOptions());

    /// [Load graph from file or generate one]
    SparseMatrix vertex_edge_global;

    if (generate_graph)
    {
        vertex_edge_global = GenerateGraph(comm, gen_vertices, mean_degree, beta, seed);
    }
    else
    {
        vertex_edge_global = ReadCSR(graph_filename);
    }

    const int nvertices_global = vertex_edge_global.Rows();
    const int nedges_global = vertex_edge_global.Cols();
    /// [Load graph from file or generate one]

    /// [Partitioning]
    std::vector<int> global_partitioning;
    if (metis_agglomeration || generate_graph)
    {
        assert(num_partitions >= num_procs);
        global_partitioning = MetisPart(vertex_edge_global, num_partitions);
    }
    else
    {
        global_partitioning = ReadText<int>(partition_filename);
    }
    /// [Partitioning]

    /// [Load the edge weights]
    std::vector<double> weight;
    if (!weight_filename.empty())
    {
        weight = linalgcpp::ReadText(weight_filename);
    }
    else
    {
        weight = std::vector<double>(nedges_global, 1.0);
    }
    /// [Load the edge weights]

    // Set up GraphUpscale
    {
        /// [Upscale]
        GraphUpscale upscale(comm, vertex_edge_global, global_partitioning,
                             spect_tol, max_evects, weight);

        upscale.PrintInfo();
        upscale.ShowSetupTime();
        /// [Upscale]

        /// [Right Hand Side]
        BlockVector fine_rhs = upscale.GetFineBlockVector();
        fine_rhs.GetBlock(0) = 0.0;

        if (generate_graph || generate_fiedler)
        {
            fine_rhs.GetBlock(1) = ComputeFiedlerVector(upscale.GetFineMatrix());
        }
        else
        {
            fine_rhs.GetBlock(1) = upscale.ReadVertexVector(fiedler_filename);
        }

        /// [Right Hand Side]

        /// [Solve]
        BlockVector upscaled_sol = upscale.Solve(fine_rhs);
        upscale.ShowCoarseSolveInfo();

        BlockVector fine_sol = upscale.SolveFine(fine_rhs);
        upscale.ShowFineSolveInfo();
        /// [Solve]

        /// [Check Error]
        upscale.ShowErrors(upscaled_sol, fine_sol);
        /// [Check Error]

        if (save_fiedler)
        {
            upscale.WriteVertexVector(fine_rhs.GetBlock(1), fiedler_filename);
        }
    }

    MPI_Finalize();
    return 0;
}

std::vector<int> MetisPart(const SparseMatrix& vertex_edge, int num_parts)
{
    SparseMatrix edge_vertex = vertex_edge.Transpose();
    SparseMatrix vertex_vertex = vertex_edge.Mult(edge_vertex);

    double ubal_tol = 2.0;

    return Partition(vertex_vertex, num_parts, ubal_tol);
}

Vector ComputeFiedlerVector(const MixedMatrix& mgl)
{
    ParMatrix A = mgl.ToPrimal();

    bool use_w = mgl.CheckW();

    if (!use_w)
    {
        A.AddDiag(1.0);
    }

    BoomerAMG boomer(A);

    int num_evects = 2;
    std::vector<Vector> evects(num_evects, Vector(A.Rows()));
    for (Vector& evect : evects)
    {
        evect.Randomize();
    }

    std::vector<double> evals = LOBPCG(A, evects, &boomer);

    assert(static_cast<int>(evals.size()) == num_evects);
    if (!use_w)
    {
        assert(std::fabs(evals[0] - 1.0) < 1e-8);
        assert(std::fabs(evals[1] - 1.0) > 1e-8);
    }

    return std::move(evects[1]);
}

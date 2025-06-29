/*
 * Copyright (c) The acados authors.
 *
 * This file is part of acados.
 *
 * The 2-Clause BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.;
 */


#include "acados/ocp_nlp/ocp_nlp_sqp_rti.h"

// external
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(ACADOS_WITH_OPENMP)
#include <omp.h>
#endif

// blasfeo
#include "blasfeo_d_aux.h"
#include "blasfeo_d_aux_ext_dep.h"
#include "blasfeo_d_blas.h"
// acados
#include "acados/ocp_nlp/ocp_nlp_common.h"
#include "acados/ocp_nlp/ocp_nlp_dynamics_cont.h"
#include "acados/ocp_nlp/ocp_nlp_reg_common.h"
#include "acados/ocp_qp/ocp_qp_common.h"
#include "acados/utils/mem.h"
#include "acados/utils/print.h"
#include "acados/utils/timing.h"
#include "acados/utils/types.h"
#include "acados/utils/strsep.h"
// acados_c
#include "acados_c/ocp_qp_interface.h"
#include "acados_c/ocp_nlp_interface.h"



/************************************************
 * options
 ************************************************/

acados_size_t ocp_nlp_sqp_rti_opts_calculate_size(void *config_, void *dims_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;

    acados_size_t size = 0;

    size += sizeof(ocp_nlp_sqp_rti_opts);

    size += ocp_nlp_opts_calculate_size(config, dims);

    return size;
}



void *ocp_nlp_sqp_rti_opts_assign(void *config_, void *dims_, void *raw_memory)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;

    char *c_ptr = (char *) raw_memory;

    ocp_nlp_sqp_rti_opts *opts = (ocp_nlp_sqp_rti_opts *) c_ptr;
    c_ptr += sizeof(ocp_nlp_sqp_rti_opts);

    opts->nlp_opts = ocp_nlp_opts_assign(config, dims, c_ptr);
    c_ptr += ocp_nlp_opts_calculate_size(config, dims);

    assert((char *) raw_memory + ocp_nlp_sqp_rti_opts_calculate_size(config,
        dims) >= c_ptr);

    return opts;
}



void ocp_nlp_sqp_rti_opts_initialize_default(void *config_,
    void *dims_, void *opts_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    ocp_nlp_opts_initialize_default(config, dims, nlp_opts);

    // SQP RTI opts
    opts->nlp_opts->warm_start_first_qp_from_nlp = true;
    opts->rti_phase = 0;
    opts->as_rti_level = STANDARD_RTI;
    opts->as_rti_advancement_strategy = SIMULATE_ADVANCE;
    opts->as_rti_iter = 0;
    opts->rti_log_residuals = 0;
    opts->rti_log_only_available_residuals = 0;

    return;
}



void ocp_nlp_sqp_rti_opts_update(void *config_, void *dims_, void *opts_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    ocp_nlp_opts_update(config, dims, nlp_opts);

    return;
}



void ocp_nlp_sqp_rti_opts_set(void *config_, void *opts_,
    const char *field, void* value)
{
    ocp_nlp_sqp_rti_opts *opts = (ocp_nlp_sqp_rti_opts *) opts_;
    ocp_nlp_config *config = config_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    char *ptr_module = NULL;
    int module_length = 0;
    char module[MAX_STR_LEN];
    extract_module_name(field, module, &module_length, &ptr_module);

    // pass options to QP module
    if ( ptr_module!=NULL && (!strcmp(ptr_module, "qp")) )
    {
        ocp_nlp_opts_set(config, nlp_opts, field, value);
    }
    else // nlp opts
    {
        if (!strcmp(field, "rti_phase"))
        {
            int* rti_phase = (int *) value;
            if (*rti_phase < 0 || *rti_phase > 2)
            {
                printf("\nerror: ocp_nlp_sqp_opts_set: invalid value for rti_phase field.\n");
                printf("possible values are: 0, 1, 2, got %d.\n", *rti_phase);
                exit(1);
            }
            opts->rti_phase = *rti_phase;
        }
        else if (!strcmp(field, "as_rti_iter"))
        {
            int* as_rti_iter = (int *) value;
            opts->as_rti_iter = *as_rti_iter;
        }
        else if (!strcmp(field, "rti_log_residuals"))
        {
            int* rti_log_residuals = (int *) value;
            opts->rti_log_residuals = *rti_log_residuals;
        }
        else if (!strcmp(field, "rti_log_only_available_residuals"))
        {
            int* rti_log_only_available_residuals = (int *) value;
            opts->rti_log_only_available_residuals = *rti_log_only_available_residuals;
        }
        else if (!strcmp(field, "as_rti_level"))
        {
            int* as_rti_level = (int *) value;
            if (*as_rti_level < LEVEL_A || *as_rti_level > STANDARD_RTI)
            {
                printf("\nerror: ocp_nlp_sqp_opts_set: invalid value for as_rti_level field.\n");
                printf("possible values are: 0, 1, 2, 3, 4, got %d.\n", *as_rti_level);
                exit(1);
            }
            opts->as_rti_level = *as_rti_level;
        }
        else
        {
            ocp_nlp_opts_set(config, nlp_opts, field, value);
        }
    }

    return;

}



void ocp_nlp_sqp_rti_opts_set_at_stage(void *config_, void *opts_, size_t stage, const char *field, void* value)
{
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = (ocp_nlp_sqp_rti_opts *) opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    ocp_nlp_opts_set_at_stage(config, nlp_opts, stage, field, value);
}



/************************************************
 * memory
 ************************************************/

acados_size_t ocp_nlp_sqp_rti_memory_calculate_size(void *config_,
    void *dims_, void *opts_, void *in_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_in *in = in_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    acados_size_t size = 0;

    size += sizeof(ocp_nlp_sqp_rti_memory);

    // nlp mem
    size += ocp_nlp_memory_calculate_size(config, dims, nlp_opts, in);

    // stat
    int stat_m = 2 + opts->as_rti_iter;
    int stat_n = 2; // qp_status, qp_iter
    if (opts->rti_log_residuals)
        stat_n += 4;  // nlp_res
    if (nlp_opts->ext_qp_res)
        stat_n += 4;  // qp_res
    size += stat_n*stat_m*sizeof(double);

    size += 8;  // initial align

    make_int_multiple_of(8, &size);

    return size;
}



void *ocp_nlp_sqp_rti_memory_assign(void *config_, void *dims_,
    void *opts_, void *in_, void *raw_memory)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_in *in = in_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    char *c_ptr = (char *) raw_memory;

    // initial align
    align_char_to(8, &c_ptr);

    ocp_nlp_sqp_rti_memory *mem = (ocp_nlp_sqp_rti_memory *) c_ptr;
    c_ptr += sizeof(ocp_nlp_sqp_rti_memory);

    // nlp mem
    mem->nlp_mem = ocp_nlp_memory_assign(config, dims, nlp_opts, in, c_ptr);
    c_ptr += ocp_nlp_memory_calculate_size(config, dims, nlp_opts, in);

    // stat
    mem->stat = (double *) c_ptr;
    mem->stat_m = 2 + opts->as_rti_iter;
    mem->stat_n = 2; // qp_status, qp_iter
    if (opts->rti_log_residuals)
        mem->stat_n += 4;  // nlp_res
    if (nlp_opts->ext_qp_res)
        mem->stat_n += 4;  // qp_res
    c_ptr += mem->stat_m*mem->stat_n*sizeof(double);

    for (int i=0; i<mem->stat_m * mem->stat_n; i++)
    {
        mem->stat[i] = 0.0;
    }

    mem->nlp_mem->status = ACADOS_READY;
    mem->is_first_call = true;

    assert((char *) raw_memory+ocp_nlp_sqp_rti_memory_calculate_size(
        config, dims, opts, in) >= c_ptr);

    return mem;
}



/************************************************
 * workspace
 ************************************************/

acados_size_t ocp_nlp_sqp_rti_workspace_calculate_size(void *config_,
    void *dims_, void *opts_, void *in_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_in *in = in_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;

    acados_size_t size = 0;

    size += sizeof(ocp_nlp_sqp_rti_workspace);

    // nlp
    size += ocp_nlp_workspace_calculate_size(config, dims, nlp_opts, in);

    return size;
}



static void ocp_nlp_sqp_rti_cast_workspace(
    ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_sqp_rti_opts *opts,
    ocp_nlp_in *nlp_in, ocp_nlp_sqp_rti_memory *mem, ocp_nlp_sqp_rti_workspace *work)
{
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;

    // sqp
    char *c_ptr = (char *) work;
    c_ptr += sizeof(ocp_nlp_sqp_rti_workspace);

    // nlp
    work->nlp_work = ocp_nlp_workspace_assign(
        config, dims, nlp_opts, nlp_in, nlp_mem, c_ptr);
    c_ptr += ocp_nlp_workspace_calculate_size(config, dims, nlp_opts, nlp_in);

    assert((char *) work + ocp_nlp_sqp_rti_workspace_calculate_size(config,
        dims, opts, nlp_in) >= c_ptr);

    return;
}


// utility functions
static void reset_stats_and_sub_timers(ocp_nlp_sqp_rti_memory *mem)
{
    mem->nlp_mem->nlp_timings->time_lin = 0.0;
    mem->nlp_mem->nlp_timings->time_reg = 0.0;
    mem->nlp_mem->nlp_timings->time_qp_sol = 0.0;
    mem->nlp_mem->nlp_timings->time_qp_solver_call = 0.0;
    mem->nlp_mem->nlp_timings->time_qp_xcond = 0.0;
    mem->nlp_mem->nlp_timings->time_glob = 0.0;
    mem->nlp_mem->iter = 0;
}



static void rti_store_residuals_in_stats(ocp_nlp_sqp_rti_opts *opts, ocp_nlp_sqp_rti_memory *mem)
{
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_res *nlp_res = nlp_mem->nlp_res;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    if (nlp_mem->iter < mem->stat_m)
    {
        int m_offset = 2 + 4 * nlp_opts->ext_qp_res;
        // printf("storing residuals AS RTI, m_offset %d\n", m_offset);
        // printf("%e\t%e\t%e\t%e\n", nlp_res->inf_norm_res_stat, nlp_res->inf_norm_res_eq, nlp_res->inf_norm_res_ineq, nlp_res->inf_norm_res_comp);
        mem->stat[mem->stat_n * nlp_mem->iter+0+m_offset] = nlp_res->inf_norm_res_stat;
        mem->stat[mem->stat_n * nlp_mem->iter+1+m_offset] = nlp_res->inf_norm_res_eq;
        mem->stat[mem->stat_n * nlp_mem->iter+2+m_offset] = nlp_res->inf_norm_res_ineq;
        mem->stat[mem->stat_n * nlp_mem->iter+3+m_offset] = nlp_res->inf_norm_res_comp;
    }
    // printf("storting residuals in line %d\n", mem->sqp_iter);
}




static void prepare_full_residual_computation(ocp_nlp_config *config,
    ocp_nlp_dims *dims, ocp_nlp_in *in, ocp_nlp_out *out, ocp_nlp_opts *opts,
    ocp_nlp_memory *mem, ocp_nlp_workspace *work)
{
    int N = dims->N;
    int *nv = dims->nv;
    int *nx = dims->nx;
    int *nu = dims->nu;
    // int *ni = dims->ni;

    for (int i=0; i < N; i++)
    {
        // dynamics: evaluate function and adjoint
        config->dynamics[i]->compute_fun_and_adj(config->dynamics[i], dims->dynamics[i], in->dynamics[i],
                                         opts->dynamics[i], mem->dynamics[i], work->dynamics[i]);
    }

    for (int i=0; i <= N; i++)
    {
        // constraints: evaluate function and adjoint
        config->constraints[i]->update_qp_matrices(config->constraints[i], dims->constraints[i], in->constraints[i],
                                         opts->constraints[i], mem->constraints[i], work->constraints[i]);
        struct blasfeo_dvec *ineq_adj =
            config->constraints[i]->memory_get_adj_ptr(mem->constraints[i]);
        blasfeo_dveccp(nv[i], ineq_adj, 0, mem->ineq_adj + i, 0);
    }

#if defined(ACADOS_WITH_OPENMP)
    #pragma omp parallel for
#endif
    for (int i=0; i <= N; i++)
    {
        // nlp mem: cost_grad
        config->cost[i]->compute_gradient(config->cost[i], dims->cost[i], in->cost[i], opts->cost[i], mem->cost[i], work->cost[i]);
        struct blasfeo_dvec *cost_grad = config->cost[i]->memory_get_grad_ptr(mem->cost[i]);
        blasfeo_dveccp(nv[i], cost_grad, 0, mem->cost_grad + i, 0);

        // nlp mem: dyn_adj
        if (i < N)
        {
            struct blasfeo_dvec *dyn_adj
                = config->dynamics[i]->memory_get_adj_ptr(mem->dynamics[i]);
            blasfeo_dveccp(nu[i] + nx[i], dyn_adj, 0, mem->dyn_adj + i, 0);
        }
        else
        {
            blasfeo_dvecse(nu[N] + nx[N], 0.0, mem->dyn_adj + N, 0);
        }
        if (i > 0)
        {
            // TODO: this could be simplified by not copying pi in the dynamics module.
            struct blasfeo_dvec *dyn_adj
                = config->dynamics[i-1]->memory_get_adj_ptr(mem->dynamics[i-1]);
            blasfeo_daxpy(nx[i], 1.0, dyn_adj, nu[i-1]+nx[i-1], mem->dyn_adj+i, nu[i],
                mem->dyn_adj+i, nu[i]);
        }
    }
}



/************************************************
 * functions
 ************************************************/

static void ocp_nlp_sqp_rti_preparation_step(ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_in *nlp_in,
    ocp_nlp_out *nlp_out, ocp_nlp_sqp_rti_opts *opts, ocp_nlp_sqp_rti_memory *mem, ocp_nlp_sqp_rti_workspace *work)
{
    acados_timer timer1;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    ocp_qp_xcond_solver_config *qp_solver = config->qp_solver;

    ocp_nlp_workspace *nlp_work = work->nlp_work;
    ocp_nlp_timings *timings = nlp_mem->nlp_timings;

    reset_stats_and_sub_timers(mem);
#if defined(ACADOS_WITH_OPENMP)
    // backup number of threads
    int num_threads_bkp = omp_get_num_threads();
    // set number of threads
    omp_set_num_threads(opts->nlp_opts->num_threads);
#endif

    // prepare submodules
    ocp_nlp_initialize_submodules(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);

    // linearize NLP and update QP matrices
    acados_tic(&timer1);
    ocp_nlp_approximate_qp_matrices(config, dims, nlp_in,
        nlp_out, nlp_opts, nlp_mem, nlp_work);
    ocp_nlp_add_levenberg_marquardt_term(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work, 1.0, 0, nlp_mem->qp_in);

    timings->time_lin += acados_toc(&timer1);

    if (opts->rti_phase == PREPARATION)
    {
        // regularize Hessian
        acados_tic(&timer1);
        config->regularize->regularize_lhs(config->regularize,
            dims->regularize, opts->nlp_opts->regularize, nlp_mem->regularize_mem);
        timings->time_reg += acados_toc(&timer1);
        // condense lhs
        acados_tic(&timer1);
        qp_solver->condense_lhs(qp_solver, dims->qp_solver,
            nlp_mem->qp_in, nlp_mem->qp_out, opts->nlp_opts->qp_solver_opts,
            nlp_mem->qp_solver_mem, nlp_work->qp_work);
        timings->time_qp_sol += acados_toc(&timer1);
    }
#if defined(ACADOS_WITH_OPENMP)
    // restore number of threads
    omp_set_num_threads(num_threads_bkp);
#endif

    return;
}


static void ocp_nlp_sqp_rti_feedback_step(ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_in *nlp_in,
    ocp_nlp_out *nlp_out, ocp_nlp_sqp_rti_opts *opts, ocp_nlp_sqp_rti_memory *mem, ocp_nlp_sqp_rti_workspace *work)
{
    acados_timer timer1;

    ocp_nlp_workspace *nlp_work = work->nlp_work;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    // ocp_qp_xcond_solver_config *qp_solver = config->qp_solver;
    ocp_nlp_timings *timings = nlp_mem->nlp_timings;

    int qp_iter = 0;
    int qp_status, globalization_status;

    // update QP rhs for SQP (step prim var, abs dual var)
    acados_tic(&timer1);
    ocp_nlp_approximate_qp_vectors_sqp(config, dims, nlp_in,
        nlp_out, nlp_opts, nlp_mem, nlp_work);
    timings->time_lin += acados_toc(&timer1);

    if (opts->rti_log_residuals)
    {
        ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
        rti_store_residuals_in_stats(opts, mem);
    }
    nlp_mem->iter += 1;

    // regularization
    acados_tic(&timer1);
    if (opts->rti_phase == FEEDBACK)
    {
        // finish regularization
        config->regularize->regularize_rhs(config->regularize,
            dims->regularize, opts->nlp_opts->regularize, nlp_mem->regularize_mem);
    }
    else if (opts->rti_phase == PREPARATION_AND_FEEDBACK)
    {
        // full regularization
        config->regularize->regularize(config->regularize,
            dims->regularize, opts->nlp_opts->regularize, nlp_mem->regularize_mem);
    }
    else
    {
        printf("ocp_nlp_sqp_rti_feedback_step: rti_phase must be FEEDBACK or PREPARATION_AND_FEEDBACK\n");
    }
    timings->time_reg += acados_toc(&timer1);

    if (nlp_opts->print_level > 0) {
        printf("\n------- qp_in --------\n");
        print_ocp_qp_in(nlp_mem->qp_in);
    }

    // set QP warm start
    if (mem->is_first_call)
    {
        if (!nlp_opts->warm_start_first_qp)
        {
            int tmp_int = 0;
            config->qp_solver->opts_set(config->qp_solver,
                opts->nlp_opts->qp_solver_opts, "warm_start", &tmp_int);
        }
        else if (nlp_opts->warm_start_first_qp_from_nlp)
        {
            int tmp_bool = true;
            config->qp_solver->opts_set(config->qp_solver, nlp_opts->qp_solver_opts, "initialize_next_xcond_qp_from_qp_out", &tmp_bool);
            ocp_nlp_initialize_qp_from_nlp(config, dims, nlp_mem->qp_in, nlp_out, nlp_mem->qp_out);
        }
    }

    // solve QP
    bool precondensed_lhs = true;
    if (opts->rti_phase == PREPARATION_AND_FEEDBACK)
    {
        precondensed_lhs = false;
    }
    qp_status = ocp_nlp_solve_qp_and_correct_dual(config, dims, nlp_opts, nlp_mem, nlp_work, precondensed_lhs, NULL, NULL, NULL, NULL, NULL);

    qp_info *qp_info_;
    ocp_qp_out_get(nlp_mem->qp_out, "qp_info", &qp_info_);
    qp_iter = qp_info_->num_iter;

    // restore default warm start
    if (mem->is_first_call)
    {
        config->qp_solver->opts_set(config->qp_solver, nlp_opts->qp_solver_opts, "warm_start", &nlp_opts->qp_warm_start);
    }

    // compute external QP residuals (for debugging)
    if (nlp_opts->ext_qp_res)
    {
        ocp_qp_res_compute(nlp_mem->qp_in, nlp_mem->qp_out, work->qp_res, work->qp_res_ws);
        ocp_qp_res_compute_nrm_inf(work->qp_res, mem->stat+(mem->stat_n*1+2));
    }
    if (nlp_opts->print_level > 0) {
        printf("\n------- qp_out --------\n");
        print_ocp_qp_out(nlp_mem->qp_out);
    }

    // save statistics
    mem->stat[mem->stat_n * nlp_mem->iter+0] = qp_status;
    mem->stat[mem->stat_n * nlp_mem->iter+1] = qp_iter;

    if ((qp_status!=ACADOS_SUCCESS) & (qp_status!=ACADOS_MAXITER))
    {
#ifndef ACADOS_SILENT
        printf("\nSQP_RTI: QP solver returned error status %d QP iteration %d.\n",
                qp_status, qp_iter);
#endif
        if (nlp_opts->print_level > 0)
        {
            printf("\n Failed to solve the following QP:\n");
            print_ocp_qp_in(nlp_mem->qp_in);
        }
        mem->nlp_mem->status = ACADOS_QP_FAILURE;
        return;
    }

    // Update variables
    double step_size;
    acados_tic(&timer1);
    globalization_status = config->globalization->find_acceptable_iterate(config, dims, nlp_in, nlp_out, nlp_mem, mem, nlp_work, nlp_opts, &step_size);
    timings->time_glob += acados_toc(&timer1);

    if (globalization_status != ACADOS_SUCCESS)
    {
        if (nlp_opts->print_level > 1)
        {
            printf("\nFailure in globalization, got status %d!\n", globalization_status);
        }
    }
    mem->nlp_mem->status = ACADOS_SUCCESS;
    mem->is_first_call = false;

    if (opts->rti_log_residuals && !opts->rti_log_only_available_residuals)
    {
        prepare_full_residual_computation(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
        ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
        rti_store_residuals_in_stats(opts, mem);
    }
}



/***************************
 * AS-RTI functionality
****************************/

static void as_rti_sanity_checks(ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_sqp_rti_opts *opts)
{
    // sanity checks
    if (dims->nx[0] != dims->nx[1])
    {
        printf("dimensions nx[0] != nx[1], cannot perform AS-RTI!\n");
        exit(1);
    }
    if (opts->as_rti_level == LEVEL_C)
    {
        int ng_ineq, ng_qp;
        // does not work for nonlinear constraints yet
        for (int k = 0; k < dims->N; k++)
        {
            config->constraints[k]->dims_get(config->constraints[k], dims->constraints[k], "ng", &ng_ineq);
            config->qp_solver->dims_get(config->qp_solver, dims->qp_solver, k, "ng", &ng_qp);
            if (ng_ineq != ng_qp)
            {
                printf("AS-RTI with LEVEL_C not implemented for nonlinear inequality constraints.");
                printf("Got ng_ineq = %d != %d = ng_qp at stage %d \n\n", ng_ineq, ng_qp, k);
                exit(1);
            }
        }
    }
    if (opts->as_rti_level == LEVEL_B)
    {
        for (int k = 0; k <= dims->N; k++)
        {
            if (dims->ns[k] > 0)
            {
                printf("\n\nAS-RTI with LEVEL_B not implemented for soft constraints yet.\n");
                printf("Got ns[%d] = %d. Exiting\n", k, dims->ns[k]);
                exit(1);
            }
        }
    }
    if (opts->as_rti_iter < 1)
    {
        printf("\n\nAS-RTI: as_rti_iter < 1: no advanced step iteration will be performed!\n\n");
    }
}

static void as_rti_advance_problem(ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_in *nlp_in, ocp_nlp_out *nlp_out, ocp_nlp_sqp_rti_opts *opts, ocp_nlp_memory *nlp_mem, ocp_nlp_workspace *nlp_work)
{
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    // setup advanced problem
    if (opts->as_rti_advancement_strategy == SHIFT_ADVANCE)
    {
        // tmp_nv_double = x at stage 1
        blasfeo_unpack_dvec(dims->nx[1], nlp_out->ux+1, dims->nu[1], nlp_work->tmp_nv_double, 1);
    }
    else if (opts->as_rti_advancement_strategy == SIMULATE_ADVANCE)
    {
        // dyn_fun = phi(x_0, u_0) - x_1
        config->dynamics[0]->compute_fun(config->dynamics[0], dims->dynamics[0], nlp_in->dynamics[0],
                            nlp_opts->dynamics[0], nlp_mem->dynamics[0], nlp_work->dynamics[0]);
        struct blasfeo_dvec *dyn_fun = config->dynamics[0]->memory_get_fun_ptr(nlp_mem->dynamics[0]);
        // dyn_fun += x_1
        blasfeo_daxpy(dims->nx[0], +1.0, nlp_out->ux+1, dims->nu[1], dyn_fun, 0, dyn_fun, 0);
        // tmp_nv_double = phi(x0, u0)
        blasfeo_unpack_dvec(dims->nx[1], dyn_fun, 0, nlp_work->tmp_nv_double, 1);
    }
    if (opts->as_rti_advancement_strategy != NO_ADVANCE)
    {
        ocp_nlp_constraints_model_set(config, dims, nlp_in, nlp_out, 0, "lbx", nlp_work->tmp_nv_double);
        ocp_nlp_constraints_model_set(config, dims, nlp_in, nlp_out, 0, "ubx", nlp_work->tmp_nv_double);
    }
    // printf("advanced x value\n");
    // blasfeo_print_exp_tran_dvec(dims->nx[1], nlp_out->ux+1, dims->nu[1]);
}



static void level_c_prepare_residual_computation(ocp_nlp_config *config,
    ocp_nlp_dims *dims, ocp_nlp_in *in, ocp_nlp_out *out, ocp_nlp_opts *opts,
    ocp_nlp_memory *mem, ocp_nlp_workspace *work)
{
    int N = dims->N;
    int *nv = dims->nv;
    int *nx = dims->nx;
    int *nu = dims->nu;
    // int *ni = dims->ni;

    // evaluate constraint adjoint
    for (int i=0; i <= N; i++)
    {
        // constraints: evaluate function and adjoint
        config->constraints[i]->update_qp_matrices(config->constraints[i], dims->constraints[i], in->constraints[i],
                                         opts->constraints[i], mem->constraints[i], work->constraints[i]);
        struct blasfeo_dvec *ineq_adj =
            config->constraints[i]->memory_get_adj_ptr(mem->constraints[i]);
        blasfeo_dveccp(nv[i], ineq_adj, 0, mem->ineq_adj + i, 0);
    }

#if defined(ACADOS_WITH_OPENMP)
    #pragma omp parallel for
#endif
    for (int i=0; i <= N; i++)
    {
        // nlp mem: cost_grad
        config->cost[i]->compute_gradient(config->cost[i], dims->cost[i], in->cost[i], opts->cost[i], mem->cost[i], work->cost[i]);
        struct blasfeo_dvec *cost_grad = config->cost[i]->memory_get_grad_ptr(mem->cost[i]);
        blasfeo_dveccp(nv[i], cost_grad, 0, mem->cost_grad + i, 0);

        // nlp mem: dyn_adj
        if (i < N)
        {
            struct blasfeo_dvec *dyn_adj
                = config->dynamics[i]->memory_get_adj_ptr(mem->dynamics[i]);
            blasfeo_dveccp(nu[i] + nx[i], dyn_adj, 0, mem->dyn_adj + i, 0);
        }
        else
        {
            blasfeo_dvecse(nu[N] + nx[N], 0.0, mem->dyn_adj + N, 0);
        }
        if (i > 0)
        {
            // TODO: this could be simplified by not copying pi in the dynamics module.
            struct blasfeo_dvec *dyn_adj
                = config->dynamics[i-1]->memory_get_adj_ptr(mem->dynamics[i-1]);
            blasfeo_daxpy(nx[i], 1.0, dyn_adj, nu[i-1]+nx[i-1], mem->dyn_adj+i, nu[i],
                mem->dyn_adj+i, nu[i]);
        }
    }
}



static void ocp_nlp_sqp_rti_preparation_advanced_step(ocp_nlp_config *config, ocp_nlp_dims *dims, ocp_nlp_in *nlp_in,
    ocp_nlp_out *nlp_out, ocp_nlp_sqp_rti_opts *opts, ocp_nlp_sqp_rti_memory *mem, ocp_nlp_sqp_rti_workspace *work)
{
    acados_timer timer1;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_timings *timings = nlp_mem->nlp_timings;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    ocp_qp_xcond_solver_config *qp_solver = config->qp_solver;

    ocp_nlp_workspace *nlp_work = work->nlp_work;
    ocp_nlp_out *tmp_nlp_out = nlp_work->tmp_nlp_out;

    reset_stats_and_sub_timers(mem);

#if defined(ACADOS_WITH_OPENMP)
    // backup number of threads
    int num_threads_bkp = omp_get_num_threads();
    // set number of threads
    omp_set_num_threads(opts->nlp_opts->num_threads);
#endif

    qp_info *qp_info_;
    int qp_iter, qp_status, globalization_status;

    // prepare submodules
    ocp_nlp_initialize_submodules(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);

    if (!mem->is_first_call)
    {
        as_rti_advance_problem(config, dims, nlp_in, nlp_out, opts, nlp_mem, nlp_work);
    }
    else
    {
        as_rti_sanity_checks(config, dims, opts);
    }

    // if AS_RTI-A and not first call!
    if (opts->as_rti_level == LEVEL_A && !mem->is_first_call)
    {
        // load iterate from tmp
        copy_ocp_nlp_out(dims, tmp_nlp_out, nlp_out);
        // perform QP solve (implemented as feedback)
        // similar to ocp_nlp_sqp_rti_feedback_step
        if (opts->rti_log_residuals && !opts->rti_log_only_available_residuals)
        {
            // NOTE: redo all residual computations after loading iterate from tmp to undo changes to memory in modules
            prepare_full_residual_computation(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
        }

        // update QP rhs for SQP (step prim var, abs dual var)
        acados_tic(&timer1);
        ocp_nlp_approximate_qp_vectors_sqp(config, dims, nlp_in,
            nlp_out, nlp_opts, nlp_mem, nlp_work);
        timings->time_lin += acados_toc(&timer1);

        if (opts->rti_log_residuals)
        {
            ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
            rti_store_residuals_in_stats(opts, mem);
        }
        nlp_mem->iter += 1;

        // regularization rhs
        acados_tic(&timer1);
        config->regularize->regularize_rhs(config->regularize,
            dims->regularize, opts->nlp_opts->regularize, nlp_mem->regularize_mem);

        // solve QP
        qp_status = ocp_nlp_solve_qp_and_correct_dual(config, dims, nlp_opts, nlp_mem, nlp_work, true, NULL, NULL, NULL, NULL, NULL);

        // save statistics
        ocp_qp_out_get(nlp_mem->qp_out, "qp_info", &qp_info_);
        qp_iter = qp_info_->num_iter;
        mem->stat[mem->stat_n * nlp_mem->iter+0] = qp_status;
        mem->stat[mem->stat_n * nlp_mem->iter+1] = qp_iter;

        if (nlp_opts->print_level > 0)
        {
            printf("\n------- qp_in AS-RTI-A preparation --------\n");
            print_ocp_qp_in(nlp_mem->qp_in);
            printf("\n------- qp_out AS-RTI-A preparation --------\n");
            print_ocp_qp_out(nlp_mem->qp_out);
        }

        // update variables
        double step_size;
        globalization_status = config->globalization->find_acceptable_iterate(config, dims, nlp_in, nlp_out, nlp_mem, mem, nlp_work, nlp_opts, &step_size);
        if (globalization_status != ACADOS_SUCCESS)
        {
            if (nlp_opts->print_level > 1)
            {
                printf("\n Failure in globalization, got status %d!\n", globalization_status);
            }
            return;
        }
    }
    else if (opts->as_rti_level == LEVEL_B && !mem->is_first_call)
    {
        // perform zero-order iterations
        for (; nlp_mem->iter < opts->as_rti_iter; nlp_mem->iter++)
        {
            acados_tic(&timer1);
            // zero order QP update
            ocp_nlp_zero_order_qp_update(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
            timings->time_lin += acados_toc(&timer1);

            if (opts->rti_log_residuals && !opts->rti_log_only_available_residuals)
            {
                // evaluate additional functions and compute residuals
                prepare_full_residual_computation(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
                ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
                rti_store_residuals_in_stats(opts, mem);
            }

            // rhs regularization
            acados_tic(&timer1);
            config->regularize->regularize_rhs(config->regularize,
                dims->regularize, nlp_opts->regularize, nlp_mem->regularize_mem);
            timings->time_reg += acados_toc(&timer1);

            // QP solve
            qp_status = ocp_nlp_solve_qp_and_correct_dual(config, dims, nlp_opts, nlp_mem, nlp_work, true, NULL, NULL, NULL, NULL, NULL);

            ocp_qp_out_get(nlp_mem->qp_out, "qp_info", &qp_info_);
            qp_iter = qp_info_->num_iter;

            // save statistics
            mem->stat[mem->stat_n * nlp_mem->iter+0] = qp_status;
            mem->stat[mem->stat_n * nlp_mem->iter+1] = qp_iter;

            // compute correct dual solution in case of Hessian regularization
            acados_tic(&timer1);
            config->regularize->correct_dual_sol(config->regularize,
                dims->regularize, nlp_opts->regularize, nlp_mem->regularize_mem);
            timings->time_reg += acados_toc(&timer1);
            if ((qp_status!=ACADOS_SUCCESS) & (qp_status!=ACADOS_MAXITER))
            {
#ifndef ACADOS_SILENT
                printf("\nSQP_RTI: QP solver returned error status %d QP iteration %d.\n",
                    qp_status, qp_iter);
#endif
                nlp_mem->status = ACADOS_QP_FAILURE;
                return;
            }

            if (nlp_opts->print_level > 0) {
                printf("\n------- qp_in B-iter %d --------\n", nlp_mem->iter);
                print_ocp_qp_in(nlp_mem->qp_in);
                printf("\n------- qp_out B-iter %d --------\n", nlp_mem->iter);
                print_ocp_qp_out(nlp_mem->qp_out);
            }

            // update variables
            double step_size;
            globalization_status = config->globalization->find_acceptable_iterate(config, dims, nlp_in, nlp_out, nlp_mem, mem, nlp_work, nlp_opts, &step_size);
            if (globalization_status != ACADOS_SUCCESS)
            {
                if (nlp_opts->print_level > 1)
                {
                    printf("\nFailure in globalization, got status %d!\n", globalization_status);
                }
                return;
            }
        }
    }
    else if (opts->as_rti_level == LEVEL_C && !mem->is_first_call)
    {
        // perform iterations
        for (; nlp_mem->iter < opts->as_rti_iter; nlp_mem->iter++)
        {
            // double norm, tmp_norm = 0.0;
            acados_tic(&timer1);
            // QP update
            ocp_nlp_level_c_update(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
            timings->time_lin += acados_toc(&timer1);

            if (opts->rti_log_residuals && !opts->rti_log_only_available_residuals)
            {
                // evaluate additional functions and compute residuals
                level_c_prepare_residual_computation(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
                ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
                rti_store_residuals_in_stats(opts, mem);
            }

            // rhs regularization
            acados_tic(&timer1);
            config->regularize->regularize_rhs(config->regularize,
                dims->regularize, nlp_opts->regularize, nlp_mem->regularize_mem);
            timings->time_reg += acados_toc(&timer1);

            // QP solve
            qp_status = ocp_nlp_solve_qp_and_correct_dual(config, dims, nlp_opts, nlp_mem, nlp_work, true, NULL, NULL, NULL, NULL, NULL);

            ocp_qp_out_get(nlp_mem->qp_out, "qp_info", &qp_info_);
            qp_iter = qp_info_->num_iter;

            // save statistics
            mem->stat[mem->stat_n * nlp_mem->iter+0] = qp_status;
            mem->stat[mem->stat_n * nlp_mem->iter+1] = qp_iter;
            if ((qp_status!=ACADOS_SUCCESS) & (qp_status!=ACADOS_MAXITER))
            {
#ifndef ACADOS_SILENT
                printf("\nSQP_RTI: QP solver returned error status %d QP iteration %d.\n",
                    qp_status, qp_iter);
#endif
                nlp_mem->status = ACADOS_QP_FAILURE;
                return;
            }

            if (nlp_opts->print_level > 0) {
                printf("\n------- qp_in B-iter %d --------\n", nlp_mem->iter);
                print_ocp_qp_in(nlp_mem->qp_in);
                printf("\n------- qp_out B-iter %d --------\n", nlp_mem->iter);
                print_ocp_qp_out(nlp_mem->qp_out);
            }

            // update variables
            double step_size;
            globalization_status = config->globalization->find_acceptable_iterate(config, dims, nlp_in, nlp_out, nlp_mem, mem, nlp_work, nlp_opts, &step_size);
            if (globalization_status != ACADOS_SUCCESS)
            {
                if (nlp_opts->print_level > 1)
                {
                    printf("\nFailure in globalization, got status %d!\n", globalization_status);
                }
                return;
            }
            // norm = 0.0;
            // for (int kk = 0; kk < dims->N; kk++)
            // {
            //     blasfeo_dvecnrm_inf(dims->nx[kk] + dims->nu[kk], nlp_mem->qp_out->ux+kk, 0, &tmp_norm);
            //     norm = (tmp_norm > norm) ? tmp_norm : norm;
            // }
            // printf("step norm primal as_rti iter %d %e\n", i, norm);
        }
    }
    else if (opts->as_rti_level == LEVEL_D)
    {
        // perform k full SQP iterations
        for (; nlp_mem->iter < opts->as_rti_iter; nlp_mem->iter++)
        {
            acados_tic(&timer1);
            // linearize NLP
            ocp_nlp_approximate_qp_matrices(config, dims, nlp_in,
                nlp_out, nlp_opts, nlp_mem, nlp_work);
            ocp_nlp_add_levenberg_marquardt_term(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work, 1.0, 0, nlp_mem->qp_in);
            ocp_nlp_approximate_qp_vectors_sqp(config, dims, nlp_in,
                nlp_out, nlp_opts, nlp_mem, nlp_work);
            timings->time_lin += acados_toc(&timer1);

            if (opts->rti_log_residuals)
            {
                ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
                rti_store_residuals_in_stats(opts, mem);
            }

            // full regularization
            acados_tic(&timer1);
            config->regularize->regularize(config->regularize,
                dims->regularize, nlp_opts->regularize, nlp_mem->regularize_mem);
            timings->time_reg += acados_toc(&timer1);

            // QP solve
            qp_status = ocp_nlp_solve_qp_and_correct_dual(config, dims, nlp_opts, nlp_mem, nlp_work, false, NULL, NULL, NULL, NULL, NULL);

            ocp_qp_out_get(nlp_mem->qp_out, "qp_info", &qp_info_);
            qp_iter = qp_info_->num_iter;

            // save statistics
            mem->stat[mem->stat_n * nlp_mem->iter+0] = qp_status;
            mem->stat[mem->stat_n * nlp_mem->iter+1] = qp_iter;

            if ((qp_status!=ACADOS_SUCCESS) & (qp_status!=ACADOS_MAXITER))
            {
#ifndef ACADOS_SILENT
                printf("\nSQP_RTI: QP solver returned error status %d QP iteration %d.\n",
                    qp_status, qp_iter);
#endif
                mem->nlp_mem->status = ACADOS_QP_FAILURE;
                return;
            }

            // update variables
            double step_size;
            globalization_status = config->globalization->find_acceptable_iterate(config, dims, nlp_in, nlp_out, nlp_mem, mem, nlp_work, nlp_opts, &step_size);
            if (globalization_status != ACADOS_SUCCESS)
            {
                if (nlp_opts->print_level > 1)
                {
                    printf("\nFailure in globalization, got status %d!\n", globalization_status);
                }
                return;
            }
        }
    }

    /* NORMAL RTI PREPARATION */
    // linearize NLP and update QP matrices
    acados_tic(&timer1);
    ocp_nlp_approximate_qp_matrices(config, dims, nlp_in,
        nlp_out, nlp_opts, nlp_mem, nlp_work);
    ocp_nlp_add_levenberg_marquardt_term(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work, 1.0, 0, nlp_mem->qp_in);
    timings->time_lin += acados_toc(&timer1);

    // regularize Hessian
    acados_tic(&timer1);
    config->regularize->regularize_lhs(config->regularize,
        dims->regularize, opts->nlp_opts->regularize, nlp_mem->regularize_mem);
    timings->time_reg += acados_toc(&timer1);
    // condense lhs
    qp_solver->condense_lhs(qp_solver, dims->qp_solver,
        nlp_mem->qp_in, nlp_mem->qp_out, opts->nlp_opts->qp_solver_opts,
        nlp_mem->qp_solver_mem, nlp_work->qp_work);
#if defined(ACADOS_WITH_OPENMP)
    // restore number of threads
    omp_set_num_threads(num_threads_bkp);
#endif

    /* AS-RTI */
    if (opts->as_rti_level == LEVEL_A)
    {
        // backup iterate:
        // tmp_nlp_out <- nlp_out
        copy_ocp_nlp_out(dims, nlp_out, tmp_nlp_out);
    }

    return;
}

int ocp_nlp_sqp_rti_setup_qp_matrices_and_factorize(void *config_, void *dims_, void *nlp_in_, void *nlp_out_,
                void *opts_, void *mem_, void *work_)
{
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_sqp_rti_workspace *work = work_;

    return ocp_nlp_common_setup_qp_matrices_and_factorize(config_, dims_, nlp_in_, nlp_out_, opts->nlp_opts, mem->nlp_mem, work->nlp_work);
}



int ocp_nlp_sqp_rti(void *config_, void *dims_, void *nlp_in_, void *nlp_out_,
    void *opts_, void *mem_, void *work_)
{
    acados_timer timer;
    acados_tic(&timer);

    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_in *nlp_in = nlp_in_;
    ocp_nlp_out *nlp_out = nlp_out_;
    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_timings *timings = mem->nlp_mem->nlp_timings;

    int rti_phase = opts->rti_phase;

    if (rti_phase == FEEDBACK)
    {
        ocp_nlp_sqp_rti_feedback_step(config, dims, nlp_in, nlp_out, opts, mem, work);
        timings->time_feedback = acados_toc(&timer);
    }
    else if (rti_phase == PREPARATION && opts->as_rti_level == STANDARD_RTI)
    {
        ocp_nlp_sqp_rti_preparation_step(config, dims, nlp_in, nlp_out, opts, mem, work);
        timings->time_preparation = acados_toc(&timer);
    }
    else if (rti_phase == PREPARATION)
    {
        ocp_nlp_sqp_rti_preparation_advanced_step(config, dims, nlp_in, nlp_out, opts, mem, work);
        timings->time_preparation = acados_toc(&timer);
    }
    else if (rti_phase == PREPARATION_AND_FEEDBACK && opts->as_rti_level != STANDARD_RTI)
    {
        printf("ocp_nlp_sqp_rti: rti_phase == PREPARATION_AND_FEEDBACK not supported with AS-RTI (opts->as_rti_level != STANDARD_RTI).\n\n");
        exit(1);
    }
    else if (rti_phase == PREPARATION_AND_FEEDBACK)
    {
        // rti_phase == PREPARATION_AND_FEEDBACK
        ocp_nlp_sqp_rti_preparation_step(config, dims, nlp_in, nlp_out, opts, mem, work);
        timings->time_preparation = acados_toc(&timer);

        acados_timer timer_feedback;
        acados_tic(&timer_feedback);
        ocp_nlp_sqp_rti_feedback_step(config, dims, nlp_in, nlp_out, opts, mem, work);
        timings->time_feedback = acados_toc(&timer_feedback);
    }
    timings->time_tot = acados_toc(&timer);

    return mem->nlp_mem->status;

}


void ocp_nlp_sqp_rti_eval_kkt_residual(void *config_, void *dims_, void *nlp_in_, void *nlp_out_,
                void *opts_, void *mem_, void *work_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_opts *nlp_opts = opts->nlp_opts;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_in *nlp_in = nlp_in_;
    ocp_nlp_out *nlp_out = nlp_out_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_workspace *nlp_work = work->nlp_work;

    ocp_nlp_initialize_submodules(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
    ocp_nlp_approximate_qp_matrices(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
    ocp_nlp_approximate_qp_vectors_sqp(config, dims, nlp_in, nlp_out, nlp_opts, nlp_mem, nlp_work);
    ocp_nlp_res_compute(dims, nlp_opts, nlp_in, nlp_out, nlp_mem->nlp_res, nlp_mem, nlp_work);
}


void ocp_nlp_sqp_rti_memory_reset_qp_solver(void *config_, void *dims_, void *nlp_in_, void *nlp_out_,
    void *opts_, void *mem_, void *work_)
{
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_qp_xcond_solver_config *qp_solver = config->qp_solver;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_workspace *nlp_work = work->nlp_work;

    mem->is_first_call = true;

    config->qp_solver->memory_reset(qp_solver, dims->qp_solver,
        nlp_mem->qp_in, nlp_mem->qp_out, opts->nlp_opts->qp_solver_opts,
        nlp_mem->qp_solver_mem, nlp_work->qp_work);
}


int ocp_nlp_sqp_rti_precompute(void *config_, void *dims_, void *nlp_in_,
    void *nlp_out_, void *opts_, void *mem_, void *work_)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_in *nlp_in = nlp_in_;
    ocp_nlp_out *nlp_out = nlp_out_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;

    nlp_mem->workspace_size = ocp_nlp_workspace_calculate_size(config, dims, opts->nlp_opts, nlp_in);

    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_sqp_rti_cast_workspace(config, dims, opts, nlp_in, mem, work);
    ocp_nlp_workspace *nlp_work = work->nlp_work;

    return ocp_nlp_precompute_common(config, dims, nlp_in, nlp_out, opts->nlp_opts, nlp_mem, nlp_work);
}



void ocp_nlp_sqp_rti_eval_param_sens(void *config_, void *dims_, void *opts_,
    void *mem_, void *work_, char *field, int stage, int index,
    void *sens_nlp_out_)
{
    acados_timer timer0;
    acados_tic(&timer0);

    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_out *sens_nlp_out = sens_nlp_out_;

    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_workspace *nlp_work = work->nlp_work;

    ocp_nlp_common_eval_param_sens(config, dims, opts->nlp_opts, nlp_mem, nlp_work,
                                 field, stage, index, sens_nlp_out);

    mem->nlp_mem->nlp_timings->time_solution_sensitivities = acados_toc(&timer0);

    return;
}


void ocp_nlp_sqp_rti_eval_lagr_grad_p(void *config_, void *dims_, void *nlp_in_, void *opts_,
    void *mem_, void *work_, const char *field, void *grad_p)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_in *nlp_in = nlp_in_;

    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_workspace *nlp_work = work->nlp_work;

    ocp_nlp_common_eval_lagr_grad_p(config, dims, nlp_in, opts->nlp_opts, nlp_mem, nlp_work, field, grad_p);

    return;
}


void ocp_nlp_sqp_rti_eval_solution_sens_adj_p(void *config_, void *dims_,
                        void *opts_, void *mem_, void *work_, void *sens_nlp_out,
                        const char *field, int stage, void *grad_p)
{
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_memory *nlp_mem = mem->nlp_mem;
    ocp_nlp_sqp_rti_workspace *work = work_;
    ocp_nlp_workspace *nlp_work = work->nlp_work;
    ocp_nlp_common_eval_solution_sens_adj_p(config, dims,
                        opts->nlp_opts, nlp_mem, nlp_work,
                        sens_nlp_out, field, stage, grad_p);
}



void ocp_nlp_sqp_rti_get(void *config_, void *dims_, void *mem_,
    const char *field, void *return_value_)
{
    ocp_nlp_config *config = config_;
    ocp_nlp_dims *dims = dims_;
    ocp_nlp_sqp_rti_memory *mem = mem_;

    char *ptr_module = NULL;
    int module_length = 0;
    char module[MAX_STR_LEN];
    extract_module_name(field, module, &module_length, &ptr_module);

    if ( ptr_module!=NULL && (!strcmp(ptr_module, "time")) )
    {
        // call timings getter
        ocp_nlp_timings_get(config, mem->nlp_mem->nlp_timings, field, return_value_);
    }
    else if (!strcmp("stat", field))
    {
        double **value = return_value_;
        *value = mem->stat;
    }
    else if (!strcmp("statistics", field))
    {
        int n_row = mem->stat_m<mem->nlp_mem->iter+1 ? mem->stat_m : mem->nlp_mem->iter+1;
        double *value = return_value_;
        for (int ii=0; ii<n_row; ii++)
        {
            value[ii+0] = ii;
            for (int jj=0; jj<mem->stat_n; jj++)
                value[ii+(jj+1)*n_row] = mem->stat[jj+ii*mem->stat_n];
        }
    }
    else if (!strcmp("stat_m", field))
    {
        int *value = return_value_;
        *value = mem->stat_m;
    }
    else if (!strcmp("stat_n", field))
    {
        int *value = return_value_;
        *value = mem->stat_n;
    }
    else if (!strcmp("qp_xcond_dims", field))
    {
        void **value = return_value_;
        *value = dims->qp_solver->xcond_dims;
    }
    else
    {
        ocp_nlp_memory_get(config, mem->nlp_mem, field, return_value_);
    }
}


void ocp_nlp_sqp_rti_opts_get(void *config_, void *dims_, void *opts_,
                          const char *field, void *return_value_)
{
    // ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_opts *opts = opts_;

    if (!strcmp("nlp_opts", field))
    {
        void **value = return_value_;
        *value = opts->nlp_opts;
    }
    else
    {
        printf("\nerror: field %s not available in ocp_nlp_sqp_rti_opts_get\n", field);
        exit(1);
    }

}


void ocp_nlp_sqp_rti_work_get(void *config_, void *dims_, void *work_,
                          const char *field, void *return_value_)
{
    // ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_workspace *work = work_;

    if (!strcmp("nlp_work", field))
    {
        void **value = return_value_;
        *value = work->nlp_work;
    }
    else
    {
        printf("\nerror: field %s not available in ocp_nlp_sqp_rti_work_get\n", field);
        exit(1);
    }
}


void ocp_nlp_sqp_rti_terminate(void *config_, void *mem_, void *work_)
{
    ocp_nlp_config *config = config_;
    ocp_nlp_sqp_rti_memory *mem = mem_;
    ocp_nlp_sqp_rti_workspace *work = work_;

    config->qp_solver->terminate(config->qp_solver, mem->nlp_mem->qp_solver_mem, work->nlp_work->qp_work);
}


bool ocp_nlp_sqp_rti_is_real_time_algorithm()
{
    return true;
}

void ocp_nlp_sqp_rti_config_initialize_default(void *config_)
{
    ocp_nlp_config *config = (ocp_nlp_config *) config_;

    config->opts_calculate_size = &ocp_nlp_sqp_rti_opts_calculate_size;
    config->opts_assign = &ocp_nlp_sqp_rti_opts_assign;
    config->opts_initialize_default = &ocp_nlp_sqp_rti_opts_initialize_default;
    config->opts_update = &ocp_nlp_sqp_rti_opts_update;
    config->opts_set = &ocp_nlp_sqp_rti_opts_set;
    config->opts_set_at_stage = &ocp_nlp_sqp_rti_opts_set_at_stage;
    config->memory_calculate_size = &ocp_nlp_sqp_rti_memory_calculate_size;
    config->memory_assign = &ocp_nlp_sqp_rti_memory_assign;
    config->workspace_calculate_size = &ocp_nlp_sqp_rti_workspace_calculate_size;
    config->evaluate = &ocp_nlp_sqp_rti;
    config->setup_qp_matrices_and_factorize = &ocp_nlp_sqp_rti_setup_qp_matrices_and_factorize;
    config->memory_reset_qp_solver = &ocp_nlp_sqp_rti_memory_reset_qp_solver;
    config->eval_param_sens = &ocp_nlp_sqp_rti_eval_param_sens;
    config->eval_lagr_grad_p = &ocp_nlp_sqp_rti_eval_lagr_grad_p;
    config->eval_solution_sens_adj_p = &ocp_nlp_sqp_rti_eval_solution_sens_adj_p;
    config->config_initialize_default = &ocp_nlp_sqp_rti_config_initialize_default;
    config->precompute = &ocp_nlp_sqp_rti_precompute;
    config->get = &ocp_nlp_sqp_rti_get;
    config->opts_get = &ocp_nlp_sqp_rti_opts_get;
    config->work_get = &ocp_nlp_sqp_rti_work_get;
    config->terminate = &ocp_nlp_sqp_rti_terminate;
    config->step_update = &ocp_nlp_update_variables_sqp;
    config->is_real_time_algorithm = &ocp_nlp_sqp_rti_is_real_time_algorithm;
    config->eval_kkt_residual = &ocp_nlp_sqp_rti_eval_kkt_residual;

    return;
}


/******************************************************************************
 * @file     suncl.cpp
 * @author   Vadim Demchik <vadimdi@yahoo.com>,
 * @author   Natalia Kolomoyets <rknv7@mail.ru>
 * @version  1.6
 *
 * @brief    [QCDGPU]
 *           Lattice simulation procedures
 *
 * @section  LICENSE
 *
 * Copyright (c) 2013-2016 Vadim Demchik, Natalia Kolomoyets
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *    Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 *****************************************************************************/
#include "suncl.h"
#include "suncpu.h"

namespace model_CL{
using model_CL::model;

using SUN_CPU::SU;
using analysis_CL::analysis;

const char* FXYZ[] = {"x","y","z","t"};   // markers for axis
const char* REIM[] = {"re","im"};         // markers for re / im

#ifndef min
#define min(a,b) ((a) < (b)) ? (a) : (b)
#endif
#ifndef max
#define max(a,b) ((a) > (b)) ? (a) : (b)
#endif

#define TIMER_FOR_ELAPSED      0  // index of timer for elapsed time calculation
#define TIMER_FOR_SIMULATIONS  1  // index of timer for  simulation time calculation
#define TIMER_FOR_SAVE         2  // index of timer for saving lattice states during simulation
#define ND_MAX                32  // maximum dimensions
#define BIN_HEADER_SIZE       64  // length of binary header (in dwords)
#define FNAME_MAX_LENGTH     250  // max length of filename with path

#define SOURCE_UPDATE       "suncl/suncl.cl"
#define SOURCE_MEASUREMENTS "suncl/sun_measurements_cl.cl"
#define SOURCE_POLYAKOV     "suncl/polyakov.cl"
#ifndef BIGLAT
#define SOURCE_WILSON_LOOP  "suncl/wilson_loop.cl"
#else
#define SOURCE_WILSON_LOOP  "BigLattice/wilson_loop.cl"
#endif

// SU(N) section __________________________________________________________________________________________
#define DATA_MEASUREMENTS   32 // number of elements for measurements

        char model::path_suncl[FILENAME_MAX]         = "suncl/";
        char model::path_kernel[FILENAME_MAX]        = "kernel/";

// end of SU(N) section -----------------------------------------------------------------------------------

// common section ____________________________________
#ifdef BIGLAT
        SubLattice::SubLattice(void)
        {
            GPU0  = new(GPU_CL::GPU);
            PRNG0 = new(PRNG_CL::PRNG);
            D_A   = new(analysis_CL::analysis);

            Analysis = (analysis_CL::analysis::data_analysis*) calloc(DATA_MEASUREMENTS,sizeof(analysis_CL::analysis::data_analysis));
        }

        SubLattice::~SubLattice(void)
        {
            delete D_A;
                D_A = 0;
            free(Analysis);

            if(Analysis_PL_Y){
                free(Analysis_PL_Y);
                free(Analysis_PL_Y_im);
                free(Analysis_PL_Z);
                free(Analysis_PL_Z_im);
            }
            
            if(Analysis_S_Y){
                free(Analysis_S_Y_s);
                free(Analysis_S_Y_t);
                free(Analysis_S_Z_s);
                free(Analysis_S_Z_t);
                free(Analysis_S_Y);
                free(Analysis_S_Z);
            }

            GPU0->device_finalize(0);
            delete(GPU0);
            GPU0 = 0;

            printf("Sublattice is deleted!\n");
        }

void        SubLattice::sublattice_analysis_SpatTemp(int data_size, model::enum_model_precision prec, int index, int index_total, unsigned int buff, int denom)
{
    //spat
    Analysis[index].pointer         = GPU0->buffer_map(buff);
    Analysis[index].denominator     = ((double) (denom));
    Analysis[index].data_name       = "aaa";//name1;
    D_A->lattice_data_analysis(&Analysis[index]);

    //temp
    Analysis[index + 1].pointer         = Analysis[index].pointer;
    Analysis[index + 1].denominator     = ((double) (denom));
    Analysis[index + 1].data_name       = "bbb";//name2;
    D_A->lattice_data_analysis(&Analysis[index + 1]);

    //total
    if(index_total > 0)
    {
        Analysis[index_total].data_name      = "ccc";//name3;
            D_A->lattice_data_analysis_joint(&Analysis[index_total],&Analysis[index],&Analysis[index + 1]);
    }
}
#endif
            model::model(void) {
        PRNG0 = new(PRNG_CL::PRNG);         // PRNG module
        D_A   = new(analysis_CL::analysis); // Data Analysis module
#ifndef CPU_RUN
        GPU0  = new(GPU_CL::GPU);           // GPU module

        // setup debug_level
        GPU0->GPU_debug.wait_for_keypress   = false;
        GPU0->GPU_debug.profiling           = false;
        GPU0->GPU_debug.rebuild_binary      = false;
        GPU0->GPU_debug.brief_report        = false;
        GPU0->GPU_debug.show_stage          = false;
        GPU0->GPU_debug.local_run           = false;

        desired_platform    = 0;
        desired_device      = 0;
        device_select       = false;

        write_lattice_state_every_secs = 15.0 * 60.0; // write lattice configuration every 15 minutes
#endif
        turnoff_config_save = false; // do not write configurations
        turnoff_prns        = false; // turn off prn production
        turnoff_updates     = false; // turn off lattice updates

        get_actions_avr     = true;  // calculate mean action values
        check_prngs         = false; // check PRNG production
#ifndef CPU_RUN
        PRNG_counter = 0;   // counter runs of subroutine PRNG_produce (for load_state purposes)
        NAV_counter  = 0;   // number of performed thermalization cycles
        ITER_counter = 0;   // number of performed working cycles
        LOAD_state   = 0;
#endif
        lattice_full_size   = new int[ND_MAX];
        lattice_domain_size = new int[ND_MAX];
#ifndef CPU_RUN
        // clear pointers
        lattice_pointer_save         = NULL;
        lattice_pointer_last         = NULL;
        lattice_pointer_initial      = NULL;
        lattice_pointer_measurements = NULL;
        prng_pointer                 = NULL;
#endif
        model_create(); // tune particular model

        Analysis = (analysis_CL::analysis::data_analysis*) calloc(DATA_MEASUREMENTS,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_X_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Y_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Z_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
    
    Analysis_S_X_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_X_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Y_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Y_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Z_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Z_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));

    Analysis_S_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
    Analysis_S_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));

#ifdef BIGLAT
    if((get_Fmunu)||(get_F0mu))
    {
        Analysis[DM_Fmunu_abs_3_re].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_3_im].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_8_re].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_8_im].data_name = (char*) calloc(15,sizeof(char));
    }
#endif
}
            model::~model(void) {
        delete[] lattice_domain_size;
        delete[] lattice_full_size;
#ifdef BIGLAT
        for (int k = 0; k < lattice_Nparts; k++)
            SubLat[k].~SubLattice();
        free(SubLat);
        free(devParts);
#ifdef USE_OPENMP
        free(devLeftParts);
#endif

        if((get_Fmunu)||(get_F0mu))
        {
            for (int i=0;i<(lattice_nd-1)*2;i++){   // loop for re and im
                free((void*)Analysis[i+DM_Fmunu_3].data_name);
                free((void*)Analysis[i+DM_Fmunu_8].data_name);
            }

            free((void*)Analysis[DM_Fmunu_abs_3_re].data_name);
            free((void*)Analysis[DM_Fmunu_abs_3_im].data_name);
            free((void*)Analysis[DM_Fmunu_abs_8_re].data_name);
            free((void*)Analysis[DM_Fmunu_abs_8_im].data_name);
        }
#endif
        delete PRNG0;
           PRNG0 = 0;
        delete D_A;
           D_A = 0;
        free(Analysis);
    
    free(Analysis_PL_X);
        free(Analysis_PL_X_im);
    free(Analysis_PL_Y);
        free(Analysis_PL_Y_im);
    free(Analysis_PL_Z);
        free(Analysis_PL_Z_im);
    free(Analysis_S_X_s);
        free(Analysis_S_X_t);
    free(Analysis_S_Y_s);
        free(Analysis_S_Y_t);
    free(Analysis_S_Z_s);
        free(Analysis_S_Z_t);
    free(Analysis_S_X);
    free(Analysis_S_Y);
    free(Analysis_S_Z);

#ifndef CPU_RUN
        free(lattice_group_elements);

        if (GPU0->GPU_debug.profiling) GPU0->print_time_detailed();

        // output elapsed time
        printf("Elapsed time: %f seconds\n",GPU0->get_timer_CPU(TIMER_FOR_ELAPSED));

        GPU0->make_finish_file(finishpath);
        GPU0->device_finalize(0);

        delete GPU0;
           GPU0 = 0;
#endif
}
#ifdef BIGLAT
void        model::lattice_set_devParts(void){
    int f = 0;
    
    for (int k = 1; k < lattice_Nparts; k++) {
        if ((SubLat[k].desired_platform == SubLat[k-1].desired_platform) && (SubLat[k].desired_device == SubLat[k-1].desired_device))
            devParts[f]++;
        else
            f++;
    }
}
#endif
void        model::lattice_get_init_file(char* file){
        int parameters_items = 0;
#ifndef CPU_RUN
        GPU_CL::GPU::GPU_init_parameters* parameters = GPU0->get_init_file(file);
#else
        init_parameters* parameters = get_init_file(file);
#endif
        if (parameters==NULL) return;
#ifdef BIGLAT
        int rnds[NPARTS];
        char prng[LENGTH];
#endif
        
        int pref_size = 5;
        
        bool parameters_flag = false;
        while(!parameters_flag){
#ifndef CPU_RUN
#ifdef BIGLAT
            if (!strcmp(parameters[parameters_items].Variable,"PLATFORM"))  {
                 for(int j = 0; j < NPARTS; j++)
                     SubLat[j].desired_platform = parameters[parameters_items].ivVarVal[j];
            }
            if (!strcmp(parameters[parameters_items].Variable,"DEVICE"))    {
                for(int j = 0; j < NPARTS; j++)
                     SubLat[j].desired_device = parameters[parameters_items].ivVarVal[j];
                device_select = true;
            }
#else
            if (!strcmp(parameters[parameters_items].Variable,"PLATFORM"))  {desired_platform   = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"DEVICE"))    {desired_device     = parameters[parameters_items].iVarVal; device_select = true;}
#endif
#endif

            if (!strcmp(parameters[parameters_items].Variable,"GROUP")) {
                lattice_group   = parameters[parameters_items].iVarVal;
                sprintf_s(fprefix, pref_size, "su%i-", lattice_group);
            }
            if (!strcmp(parameters[parameters_items].Variable,"ND"))    {lattice_nd      = parameters[parameters_items].iVarVal;}

            if (!strcmp(parameters[parameters_items].Variable,"L1"))    {
                lattice_full_size[0] = parameters[parameters_items].iVarVal;
                lattice_domain_size[0] = lattice_full_size[0];
            }
            if (!strcmp(parameters[parameters_items].Variable,"L2"))    {
                lattice_full_size[1] = parameters[parameters_items].iVarVal;
                lattice_domain_size[1] = lattice_full_size[1];
            }
            if (!strcmp(parameters[parameters_items].Variable,"L3"))    {
                lattice_full_size[2] = parameters[parameters_items].iVarVal;
                lattice_domain_size[2] = lattice_full_size[2];
            }
            if (!strcmp(parameters[parameters_items].Variable,"L4"))    {
                lattice_full_size[3] = parameters[parameters_items].iVarVal;
                lattice_domain_size[3] = lattice_full_size[3];
            }
            if (!strcmp(parameters[parameters_items].Variable,"LS"))    {
                lattice_full_size[0] = parameters[parameters_items].iVarVal;
                lattice_full_size[1] = parameters[parameters_items].iVarVal;
                lattice_full_size[2] = parameters[parameters_items].iVarVal; 
                
                lattice_domain_size[0] = lattice_full_size[0];
                lattice_domain_size[1] = lattice_full_size[1];
                lattice_domain_size[2] = lattice_full_size[2];
            }
            if (!strcmp(parameters[parameters_items].Variable,"LT"))    {
                lattice_full_size[lattice_nd - 1] = parameters[parameters_items].iVarVal;
                lattice_domain_size[lattice_nd - 1] = lattice_full_size[lattice_nd - 1];
            }
#ifdef BIGLAT
             if (!strcmp(parameters[parameters_items].Variable,"N1"))    {
                 for(int j = 0; j < NPARTS; j++){
                     SubLat[j].Nx = parameters[parameters_items].ivVarVal[j];
                     SubLat[j].Ny = lattice_full_size[1];
                     SubLat[j].Nz = lattice_full_size[2];
                     SubLat[j].Nt = lattice_full_size[3];
                 }
            }
#endif
            if (!strcmp(parameters[parameters_items].Variable,"ITER"))  {ITER            = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"NITER")) {NITER           = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"NHIT"))  {NHIT            = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"BETA"))  {BETA            = parameters[parameters_items].fVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"PHI"))   {PHI             = parameters[parameters_items].fVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"OMEGA")) {OMEGA           = parameters[parameters_items].fVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"NAV"))   {NAV             = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"INTS"))  {ints            = convert_uint_to_start(parameters[parameters_items].iVarVal);}
#ifdef BIGLAT
            if (!strcmp(parameters[parameters_items].Variable,"RANDSERIES")){   
                for(int j = 0; j < NPARTS; j++)
                     rnds[j] = parameters[parameters_items].ivVarVal[j];
            }
            if (!strcmp(parameters[parameters_items].Variable,"PRNG")){   
                strcpy_s(prng, (strlen(parameters[parameters_items].txtVarVal) + 1), parameters[parameters_items].txtVarVal);
            }
                for(int j = 0; j < NPARTS; j++){
                    SubLat[j].PRNG0->parameters_setup(parameters[parameters_items].Variable, rnds[j], prng);
                }
#else
            PRNG0->parameters_setup(parameters[parameters_items].Variable,parameters[parameters_items].iVarVal,parameters[parameters_items].txtVarVal);
#endif
            if (!strcmp(parameters[parameters_items].Variable,"OUTPUTPATH"))  {
                path = (char*) realloc(path, (strlen(parameters[parameters_items].txtVarVal) + 1) * sizeof(char));
                strcpy_s(path,(strlen(parameters[parameters_items].txtVarVal) + 1),parameters[parameters_items].txtVarVal);
            }
            if (!strcmp(parameters[parameters_items].Variable,"FINISHPATH"))  {
                finishpath = (char*) realloc(finishpath, (strlen(parameters[parameters_items].txtVarVal) + 1) * sizeof(char));
                strcpy_s(finishpath,(strlen(parameters[parameters_items].txtVarVal) + 1),parameters[parameters_items].txtVarVal);
            }
#ifndef CPU_RUN
            if (!strcmp(parameters[parameters_items].Variable,"TURNOFFWAITING"))  {
                GPU0->GPU_debug.local_run = true;
            }
            if (!strcmp(parameters[parameters_items].Variable,"REBUILDBINARY"))  {
                GPU0->GPU_debug.rebuild_binary = true;
            }
#endif
            if (!strcmp(parameters[parameters_items].Variable,"GETWILSON"))  {
                get_wilson_loop = true;
            }
            if (!strcmp(parameters[parameters_items].Variable,"GETRETRACE"))  {
                get_plaquettes_avr = true;
            }
            if (!strcmp(parameters[parameters_items].Variable,"TURNOFFFMUNU"))  {
                get_Fmunu = false;
                get_F0mu  = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU"))  {
                get_F0mu  = false;
                get_Fmunu = true;
            }
            if (!strcmp(parameters[parameters_items].Variable,"F0MU"))  {
                get_F0mu  = true;
                get_Fmunu = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU1"))  {
                get_Fmunu1 = true;
                get_Fmunu2 = false;
                get_Fmunu4 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU2"))  {
                get_Fmunu2 = true;
                get_Fmunu1 = false;
                get_Fmunu4 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU4"))  {
                get_Fmunu4 = true;
                get_Fmunu1 = false;
                get_Fmunu2 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU5"))  {
                get_Fmunu5 = true;
                get_Fmunu6 = false;
                get_Fmunu7 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU6"))  {
                get_Fmunu6 = true;
                get_Fmunu5 = false;
                get_Fmunu7 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"FMUNU7"))  {
                get_Fmunu7 = true;
                get_Fmunu5 = false;
                get_Fmunu6 = false;
            }
            if (!strcmp(parameters[parameters_items].Variable,"PL_LEVEL"))  {PL_level = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"WILSONR"))   {wilson_R = parameters[parameters_items].iVarVal;}
            if (!strcmp(parameters[parameters_items].Variable,"WILSONT"))   {wilson_T = parameters[parameters_items].iVarVal;}
            parameters_flag = parameters[parameters_items].final;
            parameters_items++;
        }
        
#ifdef BIGLAT
        lattice_set_devParts();
#endif
}

#ifndef CPU_RUN
#ifdef BIGLAT
char*       model::lattice_make_header(void){
    int header_size = 16384;
    header = (char*) calloc(header_size, sizeof(char));
    int j = 0;

    j  += sprintf_s(header+j,header_size-j, " GPU SU(%u) simulator %s (QCDGPU-m-Nx-8.1)\n\n",lattice_group,version);
#ifdef USE_OPENMP
#ifdef CHB2
    j  += sprintf_s(header+j,header_size-j, " Monte Carlo simulation of %uD SU(%u) LGT\n USE_OPENMP\t2-nd order checkerboard\n",lattice_nd,lattice_group);
#else
    j  += sprintf_s(header+j,header_size-j, " Monte Carlo simulation of %uD SU(%u) LGT\n USE_OPENMP\n",lattice_nd,lattice_group);
#endif
#else
    j  += sprintf_s(header+j,header_size-j, " Monte Carlo simulation of %uD SU(%u) LGT\n\n",lattice_nd,lattice_group);
#endif
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    for(int k = 0; k < lattice_Nparts; k++)
    {
        j  += sprintf_s(header+j,header_size-j, " Part %i:                     \n", k + 1);
    j  += sprintf_s(header+j,header_size-j, "    Active OpenCL platform   : %s\n",SubLat[k].GPU0->platform_get_name(SubLat[k].GPU0->GPU_platform));
    j  += sprintf_s(header+j,header_size-j, "    Active OpenCL device     : %s\t%s\n",SubLat[k].GPU0->GPU_info.device_name, SubLat[k].GPU0->GPU_info.device_ocl);
    }
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " lattice size                : %3u x %3u x %3u x %3u\n",lattice_full_size[0],lattice_full_size[1],lattice_full_size[2],lattice_full_size[3]);
    j  += sprintf_s(header+j,header_size-j, " ---------------------------------------------------\n");
    for(int k = 0; k < lattice_Nparts; k++)
    j  += sprintf_s(header+j,header_size-j, " Part %i                      : %3u x %3u x %3u x %3u\n", k + 1, SubLat[k].Nx, SubLat[k].Ny, SubLat[k].Nz, SubLat[k].Nt);
    j  += sprintf_s(header+j,header_size-j, " ---------------------------------------------------\n");
    j  += sprintf_s(header+j,header_size-j, " init                        : %i\n",INIT);
    if (ints == model_start_hot)
        j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 0\n");
       else
    {
           if(ints==model_start_cold)
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 1\n");
           else
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 2\n");
    }
    j  += sprintf_s(header+j,header_size-j, " ---------------------------------------------------\n");
    for(int k = 0; k < lattice_Nparts; k++){
        j  += sprintf_s(header+j,header_size-j, " Part %i\n", k + 1);
        j  += SubLat[k].PRNG0->print_generator((header+j),(header_size-j));
    }
    j  += sprintf_s(header+j,header_size-j, " ---------------------------------------------------\n");
    j  += sprintf_s(header+j,header_size-j, " nav                         : %i\n",NAV);
    j  += sprintf_s(header+j,header_size-j, " niter                       : %i\n",NITER);
    j  += sprintf_s(header+j,header_size-j, " iter (# of samples)         : %i\n",ITER);
    j  += sprintf_s(header+j,header_size-j, " nhit                        : %i\n",NHIT);
    j  += sprintf_s(header+j,header_size-j, " nhitPar                     : %i\n",NHITPar);
    if (precision == model::model_precision_single) j  += sprintf_s(header+j,header_size-j, " precision                   : single\n");
    if (precision == model::model_precision_mixed)  j  += sprintf_s(header+j,header_size-j, " precision                   : mixed\n");
    if (precision == model::model_precision_double)
    {

            if(PRNG0->PRNG_precision   == PRNG_CL::PRNG::PRNG_precision_double)
            j  += sprintf_s(header+j,header_size-j, " precision                   : full double\n");
        else
            j  += sprintf_s(header+j,header_size-j, " precision                   : double\n");
    }
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += model_make_header((header+j),(header_size-j));

    header_index = j;
    return header;
}
#else
char*       model::lattice_make_header(void){
    int header_size = 16384;
    header = (char*) calloc(header_size, sizeof(char));
    int j = 0;

    j  += sprintf_s(header+j,header_size-j, " GPU SU(%u) simulator %s (QCDGPU-m-Nx-8.1)\n\n",lattice_group,version);

    j  += sprintf_s(header+j,header_size-j, " Monte Carlo simulation of %uD SU(%u) LGT\n\n",lattice_nd,lattice_group);

    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " Active OpenCL platform   : %s\n",GPU0->platform_get_name(GPU0->GPU_platform));
    j  += sprintf_s(header+j,header_size-j, " Active OpenCL device     : %s\n",GPU0->GPU_info.device_name);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " lattice size                : %3u x %3u x %3u x %3u\n",lattice_full_size[0],lattice_full_size[1],lattice_full_size[2],lattice_full_size[3]);
    j  += sprintf_s(header+j,header_size-j, " init                        : %i\n",INIT);
    if (ints == model_start_hot)
        j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 0\n");
       else
    {
           if(ints==model_start_cold)
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 1\n");
           else
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 2\n");
    }
    j  += PRNG0->print_generator((header+j),(header_size-j));
    j  += sprintf_s(header+j,header_size-j, " nav                         : %i\n",NAV);
    j  += sprintf_s(header+j,header_size-j, " niter                       : %i\n",NITER);
    j  += sprintf_s(header+j,header_size-j, " iter (# of samples)         : %i\n",ITER);
    j  += sprintf_s(header+j,header_size-j, " nhit                        : %i\n",NHIT);
    j  += sprintf_s(header+j,header_size-j, " nhitPar                     : %i\n",NHITPar);
    if (precision == model::model_precision_single) j  += sprintf_s(header+j,header_size-j, " precision                   : single\n");
    if (precision == model::model_precision_mixed)  j  += sprintf_s(header+j,header_size-j, " precision                   : mixed\n");
    if (precision == model::model_precision_double)
    {
            if(PRNG0->PRNG_precision   == PRNG_CL::PRNG::PRNG_precision_double)
            j  += sprintf_s(header+j,header_size-j, " precision                   : full double\n");
        else
            j  += sprintf_s(header+j,header_size-j, " precision                   : double\n");
    }
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += model_make_header((header+j),(header_size-j));

    header_index = j;
    return header;
}
#endif
#else
char*       model::lattice_make_header(void){
    int header_size = 16384;
    header = (char*) calloc(header_size, sizeof(char));
    int j = 0;

    j  += sprintf_s(header+j,header_size-j, " GPU SU(%u) simulator %s (QCDGPU-m-Nx-8.1)\n\n",lattice_group,version);

    j  += sprintf_s(header+j,header_size-j, " Monte Carlo simulation of %uD SU(%u) LGT\n\n",lattice_nd,lattice_group);

    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " Sequential run on  CPU\n");
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " lattice size                : %3u x %3u x %3u x %3u\n",lattice_full_size[0],lattice_full_size[1],lattice_full_size[2],lattice_full_size[3]);
    j  += sprintf_s(header+j,header_size-j, " init                        : %i\n",INIT);
    if (ints == model_start_hot)
        j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 0\n");
       else
    {
           if(ints==model_start_cold)
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 1\n");
           else
               j  += sprintf_s(header+j,header_size-j," ints (0=hot, 1=cold, 2=gid) : 2\n");
    }
    j  += PRNG0->print_generator((header+j),(header_size-j));
    j  += sprintf_s(header+j,header_size-j, " nav                         : %i\n",NAV);
    j  += sprintf_s(header+j,header_size-j, " niter                       : %i\n",NITER);
    j  += sprintf_s(header+j,header_size-j, " iter (# of samples)         : %i\n",ITER);
    j  += sprintf_s(header+j,header_size-j, " nhit                        : %i\n",NHIT);
    j  += sprintf_s(header+j,header_size-j, " nhitPar                     : %i\n",NHITPar);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += model_make_header((header+j),(header_size-j));

    header_index = j;
    return header;
}
#endif
#ifdef BIGLAT
void        model::lattice_init(void)
{
    if (!SubLat[0].GPU0->GPU_debug.local_run) SubLat[0].GPU0->make_start_file(finishpath);
    bool supported_devices = false;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        supported_devices = false;
        if (!SubLat[k].device_select) {
            // auto-select
            supported_devices = SubLat[k].GPU0->device_auto_select(GPU_CL::GPU::GPU_vendor_any, GPU_CL::GPU::GPU_vendor_any);
        }
        else {
            // manual selection of platform and device
            supported_devices = SubLat[k].GPU0->device_select(SubLat[k].desired_platform, SubLat[k].desired_device);
        }
        if (!supported_devices){
            printf("There are no any available OpenCL devices\n");
            exit(0);
        }

#ifdef USE_OPENMP
    }

    devLeftParts = (unsigned int*)calloc(Ndevices, sizeof(unsigned int));
    devLeftParts[0] = 0;
    for (int f = 1; f < Ndevices; f++)
        devLeftParts[f] = devLeftParts[f - 1] + devParts[f - 1];

    int nthreads, tid;
    int kk;

#pragma omp parallel for private(kk)//slows down?
    for (int f = 0; f < Ndevices; f++)
        for (kk = devLeftParts[f]; kk < devLeftParts[f] + devParts[f]; kk++)
            SubLat[kk].GPU0->device_initialize(kk);

    for(int k = 0; k < lattice_Nparts; k++){
#else
    SubLat[k].GPU0->device_initialize(k);
#endif
    if(k == 0)
        SubLat[k].GPU0->print_available_hardware();
    printf(">>> Part %i:\n", k + 1);
    SubLat[k].GPU0->print_actual_hardware();
    SubLat[k].GPU0->print_stage("device initialized");

    //CHECK!!!!!
        if (INIT==0) lattice_load_state();          // load state file if needed
    }

    model_lattice_init();
}
#else
void        model::lattice_init(void)
{
#ifndef CPU_RUN
    if (!GPU0->GPU_debug.local_run) GPU0->make_start_file(finishpath);
 
    bool supported_devices = false;
    if (!device_select) {
        // auto-select
        supported_devices = GPU0->device_auto_select(GPU_CL::GPU::GPU_vendor_any,GPU_CL::GPU::GPU_vendor_any);
    } else {
        // manual selection of platform and device
        supported_devices = GPU0->device_select(desired_platform,desired_device);
    }
    if(!supported_devices){
        printf("There are no any available OpenCL devices\n");
        exit(0);
    }

    // initialize selected device & show hardwares
    GPU0->device_initialize();
    GPU0->print_available_hardware();
    GPU0->print_stage("device initialized");

    if (INIT==0) lattice_load_state();          // load state file if needed

        if (!lattice_domain_size){
            lattice_domain_size = new int[lattice_nd];
            for (int i=0;i<lattice_nd;i++) lattice_domain_size[i] = lattice_full_size[i];
        }
    
    model_lattice_init();   // model initialization
#else
    make_start_file(finishpath);
#endif
}
#endif

unsigned int model::convert_str_uint(const char* str,unsigned int offset){
    unsigned int result = 0;
    unsigned int pointer = offset;
    for (unsigned int i = 0; i<4; i++){
        if (pointer<strlen(str)) result += (str[pointer++]<<(i*8));
    }
    return result;
}

unsigned int model::convert_start_to_uint(model::model_starts start){
    if (start == model::model_start_hot)  return 0;
    if (start == model::model_start_cold) return 1;
    if (start == model::model_start_gid)  return 2;
    return 0; // return hot start otherwise
}

model::model_starts model::convert_uint_to_start(unsigned int start){
    if (start == 0) return model::model_start_hot;
    if (start == 1) return model::model_start_cold;
    if (start == 2) return model::model_start_gid;
    return model::model_start_hot; // return hot start otherwise
}

unsigned int model::convert_precision_to_uint(model::model_precision precision){
    if (precision == model::model_precision_single) return 1;
    if (precision == model::model_precision_double) return 2;
    if (precision == model::model_precision_mixed)  return 3;
    return 1; // return single precision otherwise
}

model::model_precision model::convert_uint_to_precision(unsigned int precision){
    if (precision == 1) return model::model_precision_single;
    if (precision == 2) return model::model_precision_double;
    if (precision == 3) return model::model_precision_mixed;
    return model::model_precision_single; // return single precision otherwise
}

// model-dependent section ___________________________
void        model::model_create(void){
#ifndef CPU_RUN
        // parameters for SU(N) model
        lattice_group_elements = (int*) calloc(4,sizeof(int));
        lattice_group_elements[0] =  1;
        lattice_group_elements[1] =  4;
        lattice_group_elements[2] = 12;
#endif

        turnoff_gramschmidt = false; // turn off Gram-Schmidt orthogonalization

        get_plaquettes_avr  = true;  // calculate mean plaquette values
#ifndef CPU_RUN
        get_wilson_loop     = true;  // calculate Wilson loop values
        get_Fmunu           = true;  // calculate Fmunu tensor for H field
#else
        get_wilson_loop     = false;  // calculate Wilson loop values
        get_Fmunu           = false;  // calculate Fmunu tensor for H field
        get_actions_diff   = false;
        PL_level               = 0;
#endif
        get_F0mu            = false; // calculate Fmunu tensor for E field

        get_Fmunu1          = false; // get Fmunu for lambda1 instead of lambda3
        get_Fmunu2          = false; // get Fmunu for lambda2 instead of lambda3
        get_Fmunu4          = false; // get Fmunu for lambda4 instead of lambda3

        get_Fmunu5          = false; // get Fmunu for lambda5 instead of lambda8
        get_Fmunu6          = false; // get Fmunu for lambda6 instead of lambda8
        get_Fmunu7          = false; // get Fmunu for lambda7 instead of lambda8
}

int         model::model_make_header(char* header,int header_size){
    int j = 0;

    j  += sprintf_s(header+j,header_size-j, " Wilson loop R               : %i\n",wilson_R);
    j  += sprintf_s(header+j,header_size-j, " Wilson loop T               : %i\n",wilson_T);
    if (get_Fmunu)
        j  += sprintf_s(header+j,header_size-j," FMUNU(%u, %u)\n",Fmunu_index1,Fmunu_index2);
    if (get_F0mu)
        j  += sprintf_s(header+j,header_size-j," F0MU(%u, %u)\n",Fmunu_index1,Fmunu_index2);

    j  += sprintf_s(header+j,header_size-j, " BETA                        : %16.13e\n",BETA);
    j  += sprintf_s(header+j,header_size-j, " PHI   (lambda_3)            : %16.13e\n",PHI);
    j  += sprintf_s(header+j,header_size-j, " OMEGA (lambda_8)            : %16.13e\n",OMEGA);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");

    return j;
}

char*       model::lattice_make_header2(void){
    int header_size = 16384;
    int j = header_index;

    int elapsdays,elapshours,elapsminites,elapsseconds;
    double dif;
    dif=difftime(ltimeend,ltimestart);
    elapsdays=(int)(dif/24/3600);
    elapshours=(((int) dif/3600) % 3600);
    elapsminites=(((int) dif/60) % 60);
    elapsseconds=(((int) dif) % 60);

    j  += sprintf_s(header+j,header_size-j, " Start time               : %s",timestart);
    j  += sprintf_s(header+j,header_size-j, " Finish time              : %s",timeend);
    j  += sprintf_s(header+j,header_size-j, " Elapsed time             : %i:%2.2i:%2.2i:%2.2i\n",elapsdays,elapshours,elapsminites,elapsseconds);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_S_total].data_name,Analysis[DM_S_total].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_S_total].data_name,Analysis[DM_S_total].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Plq_total].data_name,Analysis[DM_Plq_total].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Plq_total].data_name,Analysis[DM_Plq_total].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Polyakov_loop].data_name,Analysis[DM_Polyakov_loop].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop].data_name,Analysis[DM_Polyakov_loop].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Polyakov_loop_im].data_name,Analysis[DM_Polyakov_loop_im].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_im].data_name,Analysis[DM_Polyakov_loop_im].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Polyakov_loop_P2].data_name,Analysis[DM_Polyakov_loop_P2].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P2].data_name,Analysis[DM_Polyakov_loop_P2].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Polyakov_loop_P4].data_name,Analysis[DM_Polyakov_loop_P4].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P4].data_name,Analysis[DM_Polyakov_loop_P4].variance);
    j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",    Analysis[DM_Wilson_loop].data_name,Analysis[DM_Wilson_loop].mean_value);
    j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Wilson_loop].data_name,Analysis[DM_Wilson_loop].variance);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");

    for (int i=0;i<((lattice_nd-1)*2+2)*2;i++)
        j  += sprintf_s(header+j,header_size-j, " Mean %-20s: % 16.13e\n",Analysis[DM_Fmunu_3+i].data_name,Analysis[DM_Fmunu_3+i].mean_value);
    for (int i=0;i<((lattice_nd-1)*2+2)*2;i++)
        j  += sprintf_s(header+j,header_size-j, " Variance %-16s: % 16.13e\n",Analysis[DM_Fmunu_3+i].data_name,Analysis[DM_Fmunu_3+i].variance);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    for (int jj=0;jj<((lattice_nd-2)*(lattice_nd-1)+2)*2;jj++){
        j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[jj+DM_Fmunu_3].data_name,Analysis[jj+DM_Fmunu_3].GPU_last_value);
        j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[jj+DM_Fmunu_3].data_name,Analysis[jj+DM_Fmunu_3].CPU_last_value);
    }
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    for (int jj=0;jj<(lattice_nd-2)*(lattice_nd-1)*2;jj++)
        j  += sprintf_s(header+j,header_size-j, " CPU l.varnc %-13s: % 16.13e\n",Analysis[jj+DM_Fmunu_3].data_name,Analysis[jj+DM_Fmunu_3].CPU_last_variance);
    j  += sprintf_s(header+j,header_size-j, " ***************************************************\n");
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_S_total].data_name,Analysis[DM_S_total].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_S_total].data_name,Analysis[DM_S_total].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Plq_total].data_name,Analysis[DM_Plq_total].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Plq_total].data_name,Analysis[DM_Plq_total].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop].data_name,Analysis[DM_Polyakov_loop].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop].data_name,Analysis[DM_Polyakov_loop].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_im].data_name,Analysis[DM_Polyakov_loop_im].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_im].data_name,Analysis[DM_Polyakov_loop_im].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P2].data_name,Analysis[DM_Polyakov_loop_P2].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P2].data_name,Analysis[DM_Polyakov_loop_P2].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P4].data_name,Analysis[DM_Polyakov_loop_P4].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Polyakov_loop_P4].data_name,Analysis[DM_Polyakov_loop_P4].GPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " CPU last %-16s: % 16.13e\n",Analysis[DM_Wilson_loop].data_name,Analysis[DM_Wilson_loop].CPU_last_value);
    j  += sprintf_s(header+j,header_size-j, " GPU last %-16s: % 16.13e\n",Analysis[DM_Wilson_loop].data_name,Analysis[DM_Wilson_loop].GPU_last_value);
#ifndef CPU_RUN
    if (analysis_CL::analysis::results_verification)
        j  += sprintf_s(header+j,header_size-j, " *** Verification successfully passed! *************\n");
    else
        j  += sprintf_s(header+j,header_size-j, " --- Verification failed! --------------------------\n");
#endif
    header_index = j;

    return header;
}

void        model::lattice_print_measurements(void){
    printf("\n #        ");
    if (get_plaquettes_avr){
        printf(" %-11s",Analysis[DM_Plq_spat].data_name);
        printf(" %-11s",Analysis[DM_Plq_temp].data_name);
    }
    if (get_wilson_loop)
        printf(" %-11s",Analysis[DM_Wilson_loop].data_name);
    if (get_actions_avr){
        printf(" %-11s",Analysis[DM_S_spat].data_name);
        printf(" %-11s",Analysis[DM_S_temp].data_name);
    }
    if (PL_level > 0){
        printf(" %-11s",Analysis[DM_Polyakov_loop].data_name);
        printf(" %-11s",Analysis[DM_Polyakov_loop_im].data_name);
    }
    printf("\n");
    unsigned int offs = 0;
    for (int i=0; i<ITER; i++) {
        if ((i<5)||(i==(ITER - 1))){
            printf("[%5u]: ",(i+offs));
            if (get_plaquettes_avr){
                printf(" % 10.8f",Analysis[DM_Plq_spat].data[i]);
                printf(" % 10.8f",Analysis[DM_Plq_temp].data[i]);
            }
            if (get_wilson_loop)
                printf(" % 10.8f",Analysis[DM_Wilson_loop].data[i]);
            if (get_actions_avr){
                printf(" % 10.8f",Analysis[DM_S_spat].data[i]);
                printf(" % 10.8f",Analysis[DM_S_temp].data[i]);
            }
            if (PL_level > 0){
                printf(" % 10.8f",Analysis[DM_Polyakov_loop].data[i]);
                printf(" % 10.8f",Analysis[DM_Polyakov_loop_im].data[i]);
            }
            printf("\n");
        }
    }
}

#ifndef CPU_RUN
#ifdef BIGLAT
void        model::lattice_analysis_SLtoL(int index)
{
    double mean, variance;
    int i;

    Analysis[index].data  = (double*) calloc(ITER, sizeof(double));

    for (int k = 0; k < lattice_Nparts; k++)
        for(i = 0; i < ITER; i++)
                Analysis[index].data[i]  += SubLat[k].Analysis[index].data[i]  * SubLat[k].Nx;

    mean = 0.0;     variance = 0.0;
    for(i = 0; i < ITER; i++)
    {
        Analysis[index].data[i] /= lattice_full_size[0];
        if(i != 0)
            mean += Analysis[index].data[i];
    }
    mean   /= (ITER - 1);
    for (i = 1; i < ITER; i++)
        variance   += pow((Analysis[index].data[i] - mean), 2);

    Analysis[index].mean_value = mean;
    Analysis[index].variance = variance / (ITER - 1);
}

void        model::lattice_analysis_SLtoL(analysis_CL::analysis::data_analysis *analysis1, analysis_CL::analysis::data_analysis (SubLattice::*ZZ))
{
    double mean, variance;
    int i;

    (*analysis1).data  = (double*) calloc(ITER, sizeof(double));

    for (int k = 0; k < lattice_Nparts; k++)
        for(i = 0; i < ITER; i++)
                (*analysis1).data[i]  += (SubLat[k].*ZZ).data[i]  * SubLat[k].Nx;

    mean = 0.0;     variance = 0.0;
    for(i = 0; i < ITER; i++)
    {
        (*analysis1).data[i] /= lattice_full_size[0];
        if(i != 0)
            mean += (*analysis1).data[i];
    }
    mean   /= (ITER - 1);
    for (i = 1; i < ITER; i++)
        variance   += pow(((*analysis1).data[i] - mean), 2);

    (*analysis1).mean_value = mean;
    (*analysis1).variance = variance / (ITER - 1);
}

void        model::lattice_analysis1(int index_spat, int index_temp, int index_total, unsigned int (SubLattice::*ZZ))
{
    int denominator;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        denominator = SubLat[k].sublattice_sites * 3;
        SubLat[k].sublattice_analysis_SpatTemp(ITER, precision, index_spat, index_total, SubLat[k].*ZZ, denominator);
    }

    lattice_analysis_SLtoL(index_spat);
    lattice_analysis_SLtoL(index_temp);
    lattice_analysis_SLtoL(index_total);
}

void        model::lattice_analysis1(int index_spat, int index_temp, int index_total, unsigned int (SubLattice::*ZZ), int denominator)
{
    for (int k = 0; k < lattice_Nparts; k++)
        SubLat[k].sublattice_analysis_SpatTemp(ITER, precision, index_spat, index_total, SubLat[k].*ZZ, SubLat[k].denominator);

    lattice_analysis_SLtoL(index_spat);
    lattice_analysis_SLtoL(index_temp);
    if(index_total > 0)
        lattice_analysis_SLtoL(index_total);
}

void        model::lattice_analysisScalar(int index, unsigned int (SubLattice::*ZZ))
{
    for (int k = 0; k < lattice_Nparts; k++){
        SubLat[k].Analysis[index].pointer         = SubLat[k].GPU0->buffer_map(SubLat[k].*ZZ);
        SubLat[k].Analysis[index].denominator     = SubLat[k].denominator;
        SubLat[k].Analysis[index].data_name       = "aaa";
        SubLat[k].Analysis[index].storage_type         = GPU_CL::GPU::GPU_storage_double;

        SubLat[k].D_A->lattice_data_analysis(&SubLat[k].Analysis[index]);
    }
    lattice_analysis_SLtoL(index);
}

void        model::lattice_analysis_Fmunu(void)
{
    unsigned int *F_pointr;

    char *xyz[3] = {(char*)"xy", (char*)"xz", (char*)"yz"};
    char *xyzt[3] = {(char*)"xt", (char*)"yt", (char*)"zt"};
    char *reim[2] = {(char*)"re", (char*)"im"};

    for (int i=0;i<(lattice_nd-1)*2;i++){   // loop for re and im
            Analysis[i+DM_Fmunu_3].data_name = (char*) calloc(14,sizeof(char));
            Analysis[i+DM_Fmunu_8].data_name = (char*) calloc(14,sizeof(char));
    }
    
    for(int k = 0; k < lattice_Nparts; k++)
    {
        if (!SubLat[k].Analysis[DM_Plq_spat].pointer)
            F_pointr = SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_plq);
        else
            F_pointr = SubLat[k].Analysis[DM_Plq_spat].pointer;

        for (int i=0;i<(lattice_nd-1)*2;i++)   // loop for re and im
        {
            SubLat[k].Analysis[i+DM_Fmunu_3].denominator = ((double) (SubLat[k].sublattice_sites));
            SubLat[k].Analysis[i+DM_Fmunu_8].denominator = ((double) (SubLat[k].sublattice_sites));

            SubLat[k].Analysis[i+DM_Fmunu_3].pointer = F_pointr;
            SubLat[k].Analysis[i+DM_Fmunu_8].pointer = F_pointr;

            SubLat[k].Analysis[i+DM_Fmunu_3].pointer_offset = SubLat[k].sublattice_energies_offset * (1 + (i >> 1));
            SubLat[k].Analysis[i+DM_Fmunu_8].pointer_offset = SubLat[k].sublattice_energies_offset * (4 + (i >> 1));
            
            SubLat[k].D_A->lattice_data_analysis(&SubLat[k].Analysis[i+DM_Fmunu_3]);
            SubLat[k].D_A->lattice_data_analysis(&SubLat[k].Analysis[i+DM_Fmunu_8]);

            sprintf_s((char*) Analysis[i+DM_Fmunu_3].data_name,14,"Fmunu_%s_%u_%s",(get_Fmunu) ? xyz[i/2] : xyzt[i/2], Fmunu_index1, reim[(i&1)]);
            sprintf_s((char*) Analysis[i+DM_Fmunu_8].data_name,14,"Fmunu_%s_%u_%s",(get_Fmunu) ? xyz[i/2] : xyzt[i/2], Fmunu_index2, reim[(i&1)]);
        }
    }

    for (int i=0;i<(lattice_nd-1)*2;i++){
        lattice_analysis_SLtoL(i+DM_Fmunu_3);
        lattice_analysis_SLtoL(i+DM_Fmunu_8);
    }

    // Fmunu_abs_3
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_3_re], &Analysis[DM_Fmunu_xy_3_re], &Analysis[DM_Fmunu_xz_3_re], &Analysis[DM_Fmunu_yz_3_re]);
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_3_im], &Analysis[DM_Fmunu_xy_3_im], &Analysis[DM_Fmunu_xz_3_im], &Analysis[DM_Fmunu_yz_3_im]);

    // Fmunu_abs_8
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_8_re], &Analysis[DM_Fmunu_xy_8_re], &Analysis[DM_Fmunu_xz_8_re], &Analysis[DM_Fmunu_yz_8_re]);
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_8_im], &Analysis[DM_Fmunu_xy_8_im], &Analysis[DM_Fmunu_xz_8_im], &Analysis[DM_Fmunu_yz_8_im]);

    if(get_Fmunu){
        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_re].data_name,15,"Fmunu_abs_%1u_re",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_im].data_name,15,"Fmunu_abs_%1u_im",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_re].data_name,15,"Fmunu_abs_%1u_re",Fmunu_index2);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_im].data_name,15,"Fmunu_abs_%1u_im",Fmunu_index2);
    }
    if(get_F0mu){
        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_re].data_name,15,"F0mu_abs_%1u_re",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_im].data_name,15,"F0mu_abs_%1u_im",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_re].data_name,15,"F0mu_abs_%1u_re",Fmunu_index2);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_im].data_name,15,"F0mu_abs_%1u_im",Fmunu_index2);
    }
}

void        model::lattice_analysis_P2P4(void)
{
    for (int k = 0; k < lattice_Nparts; k++)
    {
        SubLat[k].Analysis[DM_Polyakov_loop_P2].pointer         = SubLat[k].Analysis[DM_Polyakov_loop].pointer;
        SubLat[k].Analysis[DM_Polyakov_loop_P2].pointer_offset  = SubLat[k].sublattice_polyakov_loop_size;
        SubLat[k].Analysis[DM_Polyakov_loop_P2].denominator     = ((double) (SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz * lattice_group * lattice_group));
        SubLat[k].D_A->lattice_data_analysis(&SubLat[k].Analysis[DM_Polyakov_loop_P2]);

        SubLat[k].Analysis[DM_Polyakov_loop_P4].pointer         = SubLat[k].Analysis[DM_Polyakov_loop].pointer;
        SubLat[k].Analysis[DM_Polyakov_loop_P4].pointer_offset  = SubLat[k].sublattice_polyakov_loop_size;
        SubLat[k].Analysis[DM_Polyakov_loop_P4].denominator     = SubLat[k].Analysis[DM_Polyakov_loop_P2].denominator * lattice_group * lattice_group;
        SubLat[k].D_A->lattice_data_analysis(&SubLat[k].Analysis[DM_Polyakov_loop_P4]);
    }

    lattice_analysis_SLtoL(DM_Polyakov_loop_P2);
    lattice_analysis_SLtoL(DM_Polyakov_loop_P4);

    Analysis[DM_Polyakov_loop_P2].data_name       = "Polyakov_loop_P2";
    Analysis[DM_Polyakov_loop_P4].data_name       = "Polyakov_loop_P4";
}

void        model::lattice_analysis_PLx(analysis_CL::analysis::data_analysis *analysiss,  unsigned int (SubLattice::*ZZ), int q, int k, int offset, int denominator)
{
    (*analysiss).data_size = ITER;

    if (precision==model_precision_double) (*analysiss).precision_single = false;
    else                                   (*analysiss).precision_single = true;

    (*analysiss).storage_type = (q == 1) ? GPU_CL::GPU::GPU_storage_double2high : GPU_CL::GPU::GPU_storage_double2low;

    (*analysiss).pointer         = SubLat[k].GPU0->buffer_map(SubLat[k].*ZZ);
    (*analysiss).pointer_offset  = offset;
    (*analysiss).denominator     = ((double) (denominator));

    D_A->lattice_data_analysis(analysiss);
}

int         model::lattice_get_part_PL(int x)
{
    int XX = 0;
    int kk;
    for(int k = 0; k < lattice_Nparts; k++){
        XX += SubLat[k].Nx;
        if(x / XX == 0){kk = k; break;}
    }

    return kk;
}

void        model::lattice_analysis_PL_diff(void)
{
    int k;
    int offset, denominator;

    Analysis_PL_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[0] + 1, sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_X_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[0] + 1, sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[1] + 1, sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Y_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[1] + 1, sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[2] + 1, sizeof(analysis_CL::analysis::data_analysis));
    Analysis_PL_Z_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[2] + 1, sizeof(analysis_CL::analysis::data_analysis));

    for(k = 0; k < lattice_Nparts; k++){
        SubLat[k].Analysis_PL_Y = (analysis_CL::analysis::data_analysis*) calloc(SubLat[k].Ny + 1, sizeof(analysis_CL::analysis::data_analysis));
        SubLat[k].Analysis_PL_Y_im = (analysis_CL::analysis::data_analysis*) calloc(SubLat[k].Ny + 1, sizeof(analysis_CL::analysis::data_analysis));
        SubLat[k].Analysis_PL_Z = (analysis_CL::analysis::data_analysis*) calloc(SubLat[k].Nz + 1, sizeof(analysis_CL::analysis::data_analysis));
        SubLat[k].Analysis_PL_Z_im = (analysis_CL::analysis::data_analysis*) calloc(SubLat[k].Nz + 1, sizeof(analysis_CL::analysis::data_analysis));
    }

    int k0 = 0;
    int Nxx = 0;
    for(int x = 0; x < lattice_full_size[0]; x++){
        k = lattice_get_part_PL(x);
        if(k - k0) Nxx += SubLat[k0].Nx;
        offset = SubLat[k].sublattice_polyakov_loop_size * (x - Nxx);
        denominator = SubLat[k].Ny * SubLat[k].Nz * lattice_group;

        Analysis_PL_X[x].data_name       = "Polyakov_loop_diff";
        lattice_analysis_PLx(&Analysis_PL_X[x], &SubLattice::sublattice_polyakov_loop_diff_x, 1, k, offset, denominator);

        Analysis_PL_X_im[x].data_name       = "Polyakov_loop_im_diff";
        lattice_analysis_PLx(&Analysis_PL_X_im[x],  &SubLattice::sublattice_polyakov_loop_diff_x, 0, k, offset, denominator);

        k0 = k;
    }

    for(k = 0; k < lattice_Nparts; k++){
        for(int y = 0; y < SubLat[k].Ny; y++){
            offset = SubLat[k].sublattice_polyakov_loop_size * y;
            denominator = SubLat[k].Nx * SubLat[k].Nz * lattice_group;
            lattice_analysis_PLx(&SubLat[k].Analysis_PL_Y[y],    &SubLattice::sublattice_polyakov_loop_diff_y, 1, k, offset, denominator);
            lattice_analysis_PLx(&SubLat[k].Analysis_PL_Y_im[y], &SubLattice::sublattice_polyakov_loop_diff_y, 0, k, offset, denominator);
        }
    }

    for(int y = 0; y < lattice_full_size[1]; y++){
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_PL_Y[y];
        lattice_analysis_SLtoL(&Analysis_PL_Y[y], &SubLattice::Analysis_PL);
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_PL_Y_im[y];
        lattice_analysis_SLtoL(&Analysis_PL_Y_im[y], &SubLattice::Analysis_PL);
    }

    for(k = 0; k < lattice_Nparts; k++){
        for(int z = 0; z < SubLat[k].Nz; z++){
            offset = SubLat[k].sublattice_polyakov_loop_size * z;
            denominator = SubLat[k].Nx * SubLat[k].Ny * lattice_group;
            lattice_analysis_PLx(&SubLat[k].Analysis_PL_Z[z], &SubLattice::sublattice_polyakov_loop_diff_z, 1, k, offset, denominator);
            lattice_analysis_PLx(&SubLat[k].Analysis_PL_Z_im[z], &SubLattice::sublattice_polyakov_loop_diff_z, 0, k, offset, denominator);
        }
    }

    for(int z = 0; z < lattice_full_size[2]; z++){
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_PL_Z[z];
        lattice_analysis_SLtoL(&Analysis_PL_Z[z], &SubLattice::Analysis_PL);
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_PL_Z_im[z];
        lattice_analysis_SLtoL(&Analysis_PL_Z_im[z], &SubLattice::Analysis_PL);
    }
}

void        model::lattice_analysis_Action_diff(void){
    int k;
    int offset, denominator;

      Analysis_S_X_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[0]+1,sizeof(analysis_CL::analysis::data_analysis));
     Analysis_S_X_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[0]+1,sizeof(analysis_CL::analysis::data_analysis));
      
      Analysis_S_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[0]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_full_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));

      for(k = 0; k < lattice_Nparts; k++){
          SubLat[k].Analysis_S_Y_s = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Ny+1, sizeof(analysis_CL::analysis::data_analysis));
          SubLat[k].Analysis_S_Y_t = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Ny+1, sizeof(analysis_CL::analysis::data_analysis));
          SubLat[k].Analysis_S_Z_s = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Nz+1, sizeof(analysis_CL::analysis::data_analysis));
          SubLat[k].Analysis_S_Z_t = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Nz+1, sizeof(analysis_CL::analysis::data_analysis));
      
          SubLat[k].Analysis_S_Y = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Ny+1, sizeof(analysis_CL::analysis::data_analysis));
          SubLat[k].Analysis_S_Z = (analysis_CL::analysis::data_analysis*)calloc(SubLat[k].Nz+1, sizeof(analysis_CL::analysis::data_analysis));
      }

    int k0 = 0;
    int Nxx = 0;
    for(int x = 0; x < lattice_full_size[0]; x++){
        k = lattice_get_part_PL(x);
        if(k - k0) Nxx += SubLat[k0].Nx;
        offset = SubLat[k].sublattice_polyakov_loop_size * (x - Nxx);
        denominator = SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt * 3;

        Analysis_S_X_s[x].data_name       = "S_s_diff";
        lattice_analysis_PLx(&Analysis_S_X_s[x], &SubLattice::sublattice_action_diff_x, 1, k, offset, denominator);
        
        Analysis_S_X_t[x].data_name       = "S_t_diff";
        lattice_analysis_PLx(&Analysis_S_X_t[x],  &SubLattice::sublattice_action_diff_x, 0, k, offset, denominator);

        Analysis_S_X[x].data_name      = "S_total_diff";
        D_A->lattice_data_analysis_joint(&Analysis_S_X[x], &Analysis_S_X_s[x], &Analysis_S_X_t[x]);

        k0 = k;
    }

    for(k = 0; k < lattice_Nparts; k++){
        for(int y = 0; y < SubLat[k].Ny; y++){
            offset = SubLat[k].sublattice_polyakov_loop_size * y;
            denominator = SubLat[k].Nx * SubLat[k].Nz * SubLat[k].Nt * 3;
            lattice_analysis_PLx(&SubLat[k].Analysis_S_Y_s[y], &SubLattice::sublattice_action_diff_y, 1, k, offset, denominator);
            lattice_analysis_PLx(&SubLat[k].Analysis_S_Y_t[y], &SubLattice::sublattice_action_diff_y, 0, k, offset, denominator);

            SubLat[k].D_A->lattice_data_analysis_joint(&SubLat[k].Analysis_S_Y[y], &SubLat[k].Analysis_S_Y_s[y], &SubLat[k].Analysis_S_Y_t[y]);
        }
        for(int z = 0; z < SubLat[k].Nz; z++){
            offset = SubLat[k].sublattice_polyakov_loop_size * z;
            denominator = SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nt * 3;
            lattice_analysis_PLx(&SubLat[k].Analysis_S_Z_s[z], &SubLattice::sublattice_action_diff_z, 1, k, offset, denominator);
            lattice_analysis_PLx(&SubLat[k].Analysis_S_Z_t[z], &SubLattice::sublattice_action_diff_z, 0, k, offset, denominator);

            SubLat[k].D_A->lattice_data_analysis_joint(&SubLat[k].Analysis_S_Z[z], &SubLat[k].Analysis_S_Z_s[z], &SubLat[k].Analysis_S_Z_t[z]);
        }
    }
    for(int y = 0; y < lattice_full_size[1]; y++){
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_S_Y[y];
        lattice_analysis_SLtoL(&Analysis_S_Y[y], &SubLattice::Analysis_PL);
    }
    for(int z = 0; z < lattice_full_size[2]; z++){
        for(k = 0; k < lattice_Nparts; k++)
            SubLat[k].Analysis_PL = SubLat[k].Analysis_S_Z[z];
        lattice_analysis_SLtoL(&Analysis_S_Z[z], &SubLattice::Analysis_PL);
    }
}

void        model::lattice_analysis(void)
{
    int i;

    for (int i=0; i<=DM_max;i++){
            Analysis[i].data_size       = ITER;
            Analysis[i].pointer_offset  = 0;
            if (precision==model_precision_double) Analysis[i].precision_single = false;
                else                               Analysis[i].precision_single = true;
            if ((i&1) == 0) Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
            else            Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
    }
    Analysis[DM_Wilson_loop].storage_type         = GPU_CL::GPU::GPU_storage_double;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        SubLat[k].Analysis = (analysis_CL::analysis::data_analysis*) calloc(DATA_MEASUREMENTS,sizeof(analysis_CL::analysis::data_analysis));
        SubLat[k].D_A   = new(analysis_CL::analysis);

        for (i=0; i<=DM_max;i++){
            SubLat[k].Analysis[i].data_size       = ITER;
            SubLat[k].Analysis[i].pointer_offset  = 0;
            if (precision==model_precision_double) SubLat[k].Analysis[i].precision_single = false;
                else                               SubLat[k].Analysis[i].precision_single = true;
            if ((i&1) == 0) SubLat[k].Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
            else            SubLat[k].Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        }
    }

    if (get_plaquettes_avr)
    {
        lattice_analysis1(DM_Plq_spat, DM_Plq_temp, DM_Plq_total, &SubLattice::sublattice_plq);
        Analysis[DM_Plq_spat].data_name       = "Plq_spat";
        Analysis[DM_Plq_temp].data_name       = "Plq_temp";
        Analysis[DM_Plq_total].data_name      = "Plq_total";
    }
    if (get_actions_avr)
    {
        lattice_analysis1(DM_S_spat, DM_S_temp, DM_S_total, &SubLattice::sublattice_energies);
        Analysis[DM_S_spat].data_name       = "S_spat";
        Analysis[DM_S_temp].data_name       = "S_temp";
        Analysis[DM_S_total].data_name      = "S_total";
    }
    if(get_actions_diff)
        lattice_analysis_Action_diff();
    if ((get_Fmunu)||(get_F0mu))
        lattice_analysis_Fmunu();
    if (PL_level > 0){
        for(int k = 0; k < lattice_Nparts; k++)
            SubLat[k].denominator = SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz * lattice_group;
        lattice_analysis1(DM_Polyakov_loop, DM_Polyakov_loop_im, -1, &SubLattice::sublattice_polyakov_loop, 0);
        Analysis[DM_Polyakov_loop].data_name       = "Polyakov_loop_re";
        Analysis[DM_Polyakov_loop_im].data_name       = "Polyakov_loop_im";
    }
    if (PL_level > 1)
        lattice_analysis_P2P4();
    if (PL_level > 2)
        lattice_analysis_PL_diff();

    if (get_wilson_loop) {
        // Wilson_loop
        for(int k = 0; k < lattice_Nparts; k++)
            SubLat[k].denominator = SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt * 3;
        lattice_analysisScalar(DM_Wilson_loop, &SubLattice::sublattice_wilson_loop);
        Analysis[DM_Wilson_loop].data_name       = "Wilson_loop";
    }
}
#else
void        model::lattice_analysis(void){
        for (int i=0; i<=DM_max;i++){
            Analysis[i].data_size       = ITER;
            Analysis[i].pointer_offset  = 0;
            if (precision==model_precision_double) Analysis[i].precision_single = false;
                else                               Analysis[i].precision_single = true;
            if ((i&1) == 0) Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
            else            Analysis[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        }

        Analysis[DM_Wilson_loop].storage_type         = GPU_CL::GPU::GPU_storage_double;

    if (get_actions_avr) {
        // S_spat
        Analysis[DM_S_spat].pointer         = GPU0->buffer_map(lattice_energies);
        Analysis[DM_S_spat].denominator     = ((double) (lattice_full_site   * 3));
        Analysis[DM_S_spat].data_name       = "S_spat";
        D_A->lattice_data_analysis(&Analysis[DM_S_spat]);

        // S_temp
        Analysis[DM_S_temp].pointer         = Analysis[DM_S_spat].pointer;
        Analysis[DM_S_temp].denominator     = ((double) (lattice_full_site   * 3));
        Analysis[DM_S_temp].data_name       = "S_temp";
        D_A->lattice_data_analysis(&Analysis[DM_S_temp]);

        // S_total
        Analysis[DM_S_total].data_name      = "S_total";
        D_A->lattice_data_analysis_joint(&Analysis[DM_S_total],&Analysis[DM_S_spat],&Analysis[DM_S_temp]);
    }
    if (get_plaquettes_avr) {
        // Plq_spat
        Analysis[DM_Plq_spat].pointer         = GPU0->buffer_map(lattice_energies_plq);
        Analysis[DM_Plq_spat].denominator     = ((double) (lattice_full_site   * 3));
        Analysis[DM_Plq_spat].data_name       = "Plq_spat";
        D_A->lattice_data_analysis(&Analysis[DM_Plq_spat]);

        // Plq_temp
        Analysis[DM_Plq_temp].pointer         = Analysis[DM_Plq_spat].pointer;
        Analysis[DM_Plq_temp].denominator     = ((double) (lattice_full_site   * 3));
        Analysis[DM_Plq_temp].data_name       = "Plq_temp";
        D_A->lattice_data_analysis(&Analysis[DM_Plq_temp]);

        // Plq_total
        Analysis[DM_Plq_total].data_name      = "Plq_total";
        D_A->lattice_data_analysis_joint(&Analysis[DM_Plq_total],&Analysis[DM_Plq_spat],&Analysis[DM_Plq_temp]);
    }    
    
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    if(get_actions_diff)
    {
          Analysis_S_X_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_X_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Y_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Y_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Z_s = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Z_t = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
      
      Analysis_S_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_S_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));

      for(int i = 0; i < lattice_domain_n1; i++)
      {
        Analysis_S_X_s[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_X_s[i].precision_single = false;
            else                                   Analysis_S_X_s[i].precision_single = true;
        Analysis_S_X_s[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_S_X_s[i].pointer         = GPU0->buffer_map(lattice_action_diff_x);
            Analysis_S_X_s[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_X_s[i].denominator     = ((double) (lattice_full_n2n3n4 * 3));
            Analysis_S_X_s[i].data_name       = "S_diff_s";
            D_A->lattice_data_analysis(&Analysis_S_X_s[i]);

        Analysis_S_X_t[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_X_t[i].precision_single = false;
            else                                   Analysis_S_X_t[i].precision_single = true;
        Analysis_S_X_t[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_S_X_t[i].pointer         = Analysis_S_X_s[i].pointer;
            Analysis_S_X_t[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_X_t[i].denominator     = ((double) (lattice_full_n2n3n4 * 3));
            Analysis_S_X_t[i].data_name       = "S_diff_t";
            D_A->lattice_data_analysis(&Analysis_S_X_t[i]);
        
        Analysis_S_X[i].data_name      = "S_total_diff";
        D_A->lattice_data_analysis_joint(&Analysis_S_X[i],&Analysis_S_X_s[i],&Analysis_S_X_t[i]);
      }
      
      int lattice_full_n1n3n4 = lattice_full_size[0] * lattice_full_size[2] * lattice_full_size[3];
      for(int i = 0; i < lattice_domain_size[1]; i++)
      {
        Analysis_S_Y_s[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_Y_s[i].precision_single = false;
            else                                   Analysis_S_Y_s[i].precision_single = true;
        Analysis_S_Y_s[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_S_Y_s[i].pointer         = GPU0->buffer_map(lattice_action_diff_y);
            Analysis_S_Y_s[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_Y_s[i].denominator     = ((double) (lattice_full_n1n3n4 * 3));
            Analysis_S_Y_s[i].data_name       = "S_diff_s";
            D_A->lattice_data_analysis(&Analysis_S_Y_s[i]);

        Analysis_S_Y_t[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_Y_t[i].precision_single = false;
            else                                   Analysis_S_Y_t[i].precision_single = true;
        Analysis_S_Y_t[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_S_Y_t[i].pointer         = Analysis_S_Y_s[i].pointer;
            Analysis_S_Y_t[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_Y_t[i].denominator     = ((double) (lattice_full_n1n3n4 * 3));
            Analysis_S_Y_t[i].data_name       = "S_diff_t";
            D_A->lattice_data_analysis(&Analysis_S_Y_t[i]);
        
        Analysis_S_Y[i].data_name      = "S_total_diff";
        D_A->lattice_data_analysis_joint(&Analysis_S_Y[i],&Analysis_S_Y_s[i],&Analysis_S_Y_t[i]);
      }
      
      int lattice_full_n1n2n4 = lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[3];
      for(int i = 0; i < lattice_domain_size[2]; i++)
      {
        Analysis_S_Z_s[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_Z_s[i].precision_single = false;
            else                                   Analysis_S_Z_s[i].precision_single = true;
        Analysis_S_Z_s[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_S_Z_s[i].pointer         = GPU0->buffer_map(lattice_action_diff_z);
            Analysis_S_Z_s[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_Z_s[i].denominator     = ((double) (lattice_full_n1n2n4 * 3));
            Analysis_S_Z_s[i].data_name       = "S_diff_s";
            D_A->lattice_data_analysis(&Analysis_S_Z_s[i]);

        Analysis_S_Z_t[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_S_Z_t[i].precision_single = false;
            else                                   Analysis_S_Z_t[i].precision_single = true;
        Analysis_S_Z_t[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_S_Z_t[i].pointer         = Analysis_S_Z_s[i].pointer;
            Analysis_S_Z_t[i].pointer_offset  = lattice_energies_size * i;
            Analysis_S_Z_t[i].denominator     = ((double) (lattice_full_n1n2n4 * 3));
            Analysis_S_Z_t[i].data_name       = "S_diff_t";
            D_A->lattice_data_analysis(&Analysis_S_Z_t[i]);
        
        Analysis_S_Z[i].data_name      = "S_total_diff";
        D_A->lattice_data_analysis_joint(&Analysis_S_Z[i],&Analysis_S_Z_s[i],&Analysis_S_Z_t[i]);
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    
    if (PL_level > 0) {
        // Polyakov_loop
        Analysis[DM_Polyakov_loop].pointer            = GPU0->buffer_map(lattice_polyakov_loop);
        Analysis[DM_Polyakov_loop].denominator        = ((double) (lattice_full_n1n2n3 * lattice_group));
        Analysis[DM_Polyakov_loop].data_name          = "Polyakov_loop";
        D_A->lattice_data_analysis(&Analysis[DM_Polyakov_loop]);

        // Polyakov_loop_im
        Analysis[DM_Polyakov_loop_im].pointer         = Analysis[DM_Polyakov_loop].pointer;
        Analysis[DM_Polyakov_loop_im].denominator     = ((double) (lattice_full_n1n2n3 * lattice_group));
        Analysis[DM_Polyakov_loop_im].data_name       = "Polyakov_loop_im";
        D_A->lattice_data_analysis(&Analysis[DM_Polyakov_loop_im]);
    
//*************************************************************************************
    if(PL_level > 2)
    {
      Analysis_PL_X = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_PL_X_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_n1+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_PL_Y = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_PL_Y_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[1]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_PL_Z = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
      Analysis_PL_Z_im = (analysis_CL::analysis::data_analysis*) calloc(lattice_domain_size[2]+1,sizeof(analysis_CL::analysis::data_analysis));
      
      for(int i = 0; i < lattice_domain_n1; i++)
      {
        Analysis_PL_X[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_X[i].precision_single = false;
            else                                   Analysis_PL_X[i].precision_single = true;
        Analysis_PL_X[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_PL_X[i].pointer         = GPU0->buffer_map(lattice_polyakov_loop_diff_x);
            Analysis_PL_X[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_X[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_X[i].data_name       = "Polyakov_loop_diff";
            D_A->lattice_data_analysis(&Analysis_PL_X[i]);

        Analysis_PL_X_im[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_X_im[i].precision_single = false;
            else                                   Analysis_PL_X_im[i].precision_single = true;
        Analysis_PL_X_im[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_PL_X_im[i].pointer         = Analysis_PL_X[i].pointer;
            Analysis_PL_X_im[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_X_im[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_X_im[i].data_name       = "Polyakov_loop_im_diff";
            D_A->lattice_data_analysis(&Analysis_PL_X_im[i]);
      }
      
      for(int i = 0; i < lattice_domain_size[1]; i++)
      {
        Analysis_PL_Y[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_Y[i].precision_single = false;
            else                                   Analysis_PL_Y[i].precision_single = true;
        Analysis_PL_Y[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_PL_Y[i].pointer         = GPU0->buffer_map(lattice_polyakov_loop_diff_y);
            Analysis_PL_Y[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_Y[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_Y[i].data_name       = "Polyakov_loop_diff";
            D_A->lattice_data_analysis(&Analysis_PL_Y[i]);

        Analysis_PL_Y_im[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_Y_im[i].precision_single = false;
            else                                   Analysis_PL_Y_im[i].precision_single = true;
        Analysis_PL_Y_im[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_PL_Y_im[i].pointer         = Analysis_PL_Y[i].pointer;
            Analysis_PL_Y_im[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_Y_im[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_Y_im[i].data_name       = "Polyakov_loop_im_diff";
            D_A->lattice_data_analysis(&Analysis_PL_Y_im[i]);
      }
      
      for(int i = 0; i < lattice_domain_size[2]; i++)
      {
        Analysis_PL_Z[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_Z[i].precision_single = false;
            else                                   Analysis_PL_Z[i].precision_single = true;
        Analysis_PL_Z[i].storage_type = GPU_CL::GPU::GPU_storage_double2high;
        
        Analysis_PL_Z[i].pointer         = GPU0->buffer_map(lattice_polyakov_loop_diff_z);
            Analysis_PL_Z[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_Z[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_Z[i].data_name       = "Polyakov_loop_diff";
            D_A->lattice_data_analysis(&Analysis_PL_Z[i]);

        Analysis_PL_Z_im[i].data_size = ITER;
        
        if (precision==model_precision_double) Analysis_PL_Z_im[i].precision_single = false;
            else                                   Analysis_PL_Z_im[i].precision_single = true;
        Analysis_PL_Z_im[i].storage_type = GPU_CL::GPU::GPU_storage_double2low;
        
        Analysis_PL_Z_im[i].pointer         = Analysis_PL_Z[i].pointer;
            Analysis_PL_Z_im[i].pointer_offset  = lattice_polyakov_loop_size * i;
            Analysis_PL_Z_im[i].denominator     = ((double) (lattice_full_n2n3 * lattice_group));
            Analysis_PL_Z_im[i].data_name       = "Polyakov_loop_im_diff";
            D_A->lattice_data_analysis(&Analysis_PL_Z_im[i]);
      }
      
    }
//*************************************************************************************

    }
    if (PL_level > 1){
        // Polyakov_loop_P2
        Analysis[DM_Polyakov_loop_P2].pointer         = Analysis[DM_Polyakov_loop].pointer;
        Analysis[DM_Polyakov_loop_P2].pointer_offset  = lattice_polyakov_loop_size;
        Analysis[DM_Polyakov_loop_P2].denominator     = ((double) (lattice_full_n1n2n3 * lattice_group * lattice_group));
        Analysis[DM_Polyakov_loop_P2].data_name       = "Polyakov_loop_P2";
        D_A->lattice_data_analysis(&Analysis[DM_Polyakov_loop_P2]);

        // Polyakov_loop_P4
        Analysis[DM_Polyakov_loop_P4].pointer         = Analysis[DM_Polyakov_loop].pointer;
        Analysis[DM_Polyakov_loop_P4].pointer_offset  = lattice_polyakov_loop_size;
        Analysis[DM_Polyakov_loop_P4].denominator     = ((double) (lattice_full_n1n2n3 * lattice_group * lattice_group * lattice_group * lattice_group));
        Analysis[DM_Polyakov_loop_P4].data_name       = "Polyakov_loop_P4";
        D_A->lattice_data_analysis(&Analysis[DM_Polyakov_loop_P4]);
    }
    if (get_wilson_loop) {
        // Wilson_loop
        Analysis[DM_Wilson_loop].pointer         = GPU0->buffer_map(lattice_wilson_loop);
        Analysis[DM_Wilson_loop].denominator     = ((double) (lattice_full_site * 3));
        Analysis[DM_Wilson_loop].data_name       = "Wilson_loop";
        D_A->lattice_data_analysis(&Analysis[DM_Wilson_loop]);
    }
    if ((get_Fmunu)||(get_F0mu)) {
        // Fmunu_xy_3_re
        unsigned int* F_pointr;
        if (!Analysis[DM_Plq_spat].pointer)
            F_pointr = GPU0->buffer_map(lattice_energies_plq);
        else
            F_pointr = Analysis[DM_Plq_spat].pointer;

        int index_x = 0;
        int index_y = 1;
        for (int i=0;i<(lattice_nd-1)*2;i++){   // loop for re and im
            Analysis[i+DM_Fmunu_3].denominator = ((double) (lattice_full_site));
            Analysis[i+DM_Fmunu_8].denominator = ((double) (lattice_full_site));

            Analysis[i+DM_Fmunu_3].pointer = F_pointr;
            Analysis[i+DM_Fmunu_8].pointer = F_pointr;

            Analysis[i+DM_Fmunu_3].pointer_offset = lattice_energies_offset * (1 + (i >> 1));
            Analysis[i+DM_Fmunu_8].pointer_offset = lattice_energies_offset * (4 + (i >> 1));

            Analysis[i+DM_Fmunu_3].data_name = (char*) calloc(14,sizeof(char));
            Analysis[i+DM_Fmunu_8].data_name = (char*) calloc(14,sizeof(char));

            if (get_Fmunu) {
                sprintf_s((char*) Analysis[i+DM_Fmunu_3].data_name,14,"Fmunu_%s%s_%u_%s",FXYZ[index_x],FXYZ[index_y],Fmunu_index1,REIM[(i&1)]);
                sprintf_s((char*) Analysis[i+DM_Fmunu_8].data_name,14,"Fmunu_%s%s_%u_%s",FXYZ[index_x],FXYZ[index_y],Fmunu_index2,REIM[(i&1)]);
            } else {
                sprintf_s((char*) Analysis[i+DM_Fmunu_3].data_name,14,"Fmunu_%st_%u_%s",FXYZ[((i>>1)&3)],Fmunu_index1,REIM[(i&1)]);
                sprintf_s((char*) Analysis[i+DM_Fmunu_8].data_name,14,"Fmunu_%st_%u_%s",FXYZ[((i>>1)&3)],Fmunu_index2,REIM[(i&1)]);
            }

            D_A->lattice_data_analysis(&Analysis[i+DM_Fmunu_3]);
            D_A->lattice_data_analysis(&Analysis[i+DM_Fmunu_8]);

            if ((i&1)==1) {
                index_y++;
                if (index_y>(lattice_nd-2)) {
                    index_x++;
                    index_y=index_x+1;
                }
            }
        }

        Analysis[DM_Fmunu_abs_3_re].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_3_im].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_8_re].data_name = (char*) calloc(15,sizeof(char));
        Analysis[DM_Fmunu_abs_8_im].data_name = (char*) calloc(15,sizeof(char));

        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_re].data_name,15,"Fmunu_abs_%1u_re",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_3_im].data_name,15,"Fmunu_abs_%1u_im",Fmunu_index1);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_re].data_name,15,"Fmunu_abs_%1u_re",Fmunu_index2);
        sprintf_s((char*) Analysis[DM_Fmunu_abs_8_im].data_name,15,"Fmunu_abs_%1u_im",Fmunu_index2);

        // Fmunu_abs_3
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_3_re],&Analysis[DM_Fmunu_xy_3_re],&Analysis[DM_Fmunu_xz_3_re],&Analysis[DM_Fmunu_yz_3_re]);
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_3_im],&Analysis[DM_Fmunu_xy_3_im],&Analysis[DM_Fmunu_xz_3_im],&Analysis[DM_Fmunu_yz_3_im]);

        // Fmunu_abs_8
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_8_re],&Analysis[DM_Fmunu_xy_8_re],&Analysis[DM_Fmunu_xz_8_re],&Analysis[DM_Fmunu_yz_8_re]);
        D_A->lattice_data_analysis_joint3(&Analysis[DM_Fmunu_abs_8_im],&Analysis[DM_Fmunu_xy_8_im],&Analysis[DM_Fmunu_xz_8_im],&Analysis[DM_Fmunu_yz_8_im]);
    }
    
}
#endif
#endif

void        model::lattice_write_results(void) {
    FILE *stream;
    char buffer[250];
    int j;

    char* header2 = lattice_make_header2();
    printf("%s\n",header2);

    j  = sprintf_s(buffer  ,sizeof(buffer),  "%s",path);
    j += sprintf_s(buffer+j,sizeof(buffer)-j,"%s",fprefix);
    j += sprintf_s(buffer+j,sizeof(buffer)-j,"%.2s-%.3s-%.2s-%.2s-%.2s-%.2s.txt",timeend+22,timeend+4,timeend+8,timeend+11,timeend+14,timeend+17);
    
    fopen_s(&stream,buffer,"w+");
    if(stream)
    {
        fprintf(stream,header);
    
    if (PL_level>2) {
      fprintf(stream, " ***************************************************\n");
      fprintf(stream,"Differentiated Polyakov loop data (#, PL, PL_im, PL_variance, PL_im_variance):");

      fprintf(stream, "\n"); 
      fprintf(stream,"\nX              ");  
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nPL             ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_X[i].mean_value);
      fprintf(stream,"\nPL_im          ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_X_im[i].mean_value);
      fprintf(stream,"\nPL_variance    ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_X[i].variance);
      fprintf(stream,"\nPL_im_variance ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_X_im[i].variance);
      fprintf(stream,"\n");
       
      fprintf(stream,"\nY              ");  
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nPL             ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Y[i].mean_value);
      fprintf(stream,"\nPL_im          ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Y_im[i].mean_value);
      fprintf(stream,"\nPL_variance    ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Y[i].variance);
      fprintf(stream,"\nPL_im_variance ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Y_im[i].variance);
      fprintf(stream,"\n");
      
      fprintf(stream,"\nZ              ");  
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nPL             ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Z[i].mean_value);
      fprintf(stream,"\nPL_im          ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Z_im[i].mean_value);
      fprintf(stream,"\nPL_variance    ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Z[i].variance);
      fprintf(stream,"\nPL_im_variance ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_PL_Z_im[i].variance);
      fprintf(stream,"\n");
    }
    
    if(get_actions_diff)
    {
      fprintf(stream, " ***************************************************\n");
      fprintf(stream,"Differentiated S data (#, S_total, S_variance):");

      fprintf(stream, "\n"); 
      fprintf(stream,"\nX              ");  
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nS             ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_X[i].mean_value);
      fprintf(stream,"\nS_variance    ");
      for (int i=0; i<lattice_full_size[0];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_X[i].variance);
      fprintf(stream,"\n");
       
      fprintf(stream,"\nY              ");  
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nS             ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_Y[i].mean_value);
      fprintf(stream,"\nS_variance    ");
      for (int i=0; i<lattice_full_size[1];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_Y[i].variance);
      fprintf(stream,"\n");
      
      fprintf(stream,"\nZ              ");  
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "%2u                   ",i);
      fprintf(stream,"\nS             ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_Z[i].mean_value);
      fprintf(stream,"\nS_variance    ");
      for (int i=0; i<lattice_full_size[2];i++)
            fprintf(stream, "% 16.13e ",Analysis_S_Z[i].variance);
      fprintf(stream,"\n");
    }

        fprintf(stream, " ***************************************************\n");
        fprintf(stream, " Data fields:\n");
        fprintf(stream, "    #,  ");
        if (get_plaquettes_avr){
            fprintf(stream, "%-21s ",Analysis[DM_Plq_spat].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_Plq_temp].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_Plq_total].data_name);
        }
        if (get_wilson_loop)
            fprintf(stream, "%-21s ",Analysis[DM_Wilson_loop].data_name);
        if (get_actions_avr){
            fprintf(stream, "%-21s ",Analysis[DM_S_spat].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_S_temp].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_S_total].data_name);
        }
        if (PL_level > 0){
            fprintf(stream, "%-21s ",Analysis[DM_Polyakov_loop].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_Polyakov_loop_im].data_name);
        }
        if (PL_level > 1){
            fprintf(stream, "%-21s ",Analysis[DM_Polyakov_loop_P2].data_name);
            fprintf(stream, "%-21s ",Analysis[DM_Polyakov_loop_P4].data_name);
        }
        if ((get_Fmunu)||(get_F0mu))
        for (int i=0;i<((lattice_nd-1)*2+2)*2;i++)
            fprintf(stream, "%-21s ",Analysis[DM_Fmunu_3+i].data_name);
            fprintf(stream, "\n");
            fprintf(stream, " ***************************************************\n");

        // write plaquette data
        for (int i=0; i<ITER; i++) {
                                         fprintf(stream, "%5i",i);
            if (get_plaquettes_avr){
                fprintf(stream, " % 16.13e",Analysis[DM_Plq_spat].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_Plq_temp].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_Plq_total].data[i]);
            }
            if (get_wilson_loop)
                fprintf(stream, " % 16.13e",Analysis[DM_Wilson_loop].data[i]);
            if (get_actions_avr){
                fprintf(stream, " % 16.13e",Analysis[DM_S_spat].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_S_temp].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_S_total].data[i]);
            }
            if (PL_level > 0){
                fprintf(stream, " % 16.13e",Analysis[DM_Polyakov_loop].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_Polyakov_loop_im].data[i]);
            }
            if (PL_level > 1){
                fprintf(stream, " % 16.13e",Analysis[DM_Polyakov_loop_P2].data[i]);
                fprintf(stream, " % 16.13e",Analysis[DM_Polyakov_loop_P4].data[i]);
            }
            if ((get_Fmunu)||(get_F0mu))
                for (int jj=0;jj<((lattice_nd-2)*(lattice_nd-1)+2)*2;jj++){
                    fprintf(stream, " % 16.13e",Analysis[jj+DM_Fmunu_3].data[i]);
                }
            fprintf(stream, "\n");
        }

        if ( fclose(stream) ) {printf( "The file was not closed!\n" ); }
    }
}

#ifndef CPU_RUN
void        model::lattice_save_state(void){
    time_t ltimesave;
    time(&ltimesave);
    char* timesave   = GPU0->get_current_datetime();

    lattice_pointer_save       = GPU0->buffer_map(lattice_table);
    unsigned int* lattice_measurement_save   = GPU0->buffer_map(lattice_measurement);
    unsigned int* lattice_energies_save      = GPU0->buffer_map(lattice_energies);
    unsigned int* lattice_energies_plq_save  = NULL;
    unsigned int* lattice_wilson_loop_save   = NULL;
    unsigned int* lattice_polyakov_loop_save = NULL;

    if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu))
        lattice_energies_plq_save  = GPU0->buffer_map(lattice_energies_plq);
    if (get_wilson_loop)
        lattice_wilson_loop_save   = GPU0->buffer_map(lattice_wilson_loop);
    if (PL_level > 0)
        lattice_polyakov_loop_save = GPU0->buffer_map(lattice_polyakov_loop);

    FILE *stream;
    char buffer[250];
    int j = 0;

    unsigned int* head = lattice_make_bin_header();

    j  = sprintf_s(buffer  ,sizeof(buffer),  "%s",path);
    j += sprintf_s(buffer+j,sizeof(buffer)-j,"%s",fprefix);
    j += sprintf_s(buffer+j,sizeof(buffer)-j,"%.2s-%.3s-%.2s-%.2s-%.2s-%.2s.qcg",timesave+22,timesave+4,timesave+8,timesave+11,timesave+14,timesave+17);

    fopen_s(&stream,buffer,"wb");
    if(stream)
    {
        fwrite(head,sizeof(unsigned int),BIN_HEADER_SIZE,stream);                                       // write header
        fwrite(lattice_measurement_save, sizeof(cl_double2), lattice_measurement_size_F, stream);       // write measurements
        fwrite(lattice_energies_save,    sizeof(cl_double2), lattice_energies_size, stream);            // write energies
        if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu))
            fwrite(lattice_energies_plq_save,  sizeof(cl_double2), lattice_energies_size_F, stream);    // write energies_plq
        if (get_wilson_loop)
            fwrite(lattice_wilson_loop_save,   sizeof(cl_double),  lattice_energies_size, stream);      // write wilson loop
        if (PL_level > 0)
            fwrite(lattice_polyakov_loop_save, sizeof(cl_double2), lattice_polyakov_loop_size, stream); // write polyakov loop
        if (precision == model_precision_single)                                                        // write configuration
            fwrite(lattice_pointer_save, sizeof(cl_float4), lattice_table_size, stream);
        else
            fwrite(lattice_pointer_save, sizeof(cl_double4), lattice_table_size, stream);

        unsigned int hlen  = BIN_HEADER_SIZE*sizeof(unsigned int);
            if (GPU0->GPU_debug.brief_report) printf("Header: 0x%X-0x%X\n",0,hlen);
        unsigned int hlen2 = lattice_measurement_size_F * sizeof(cl_double2);
            if (GPU0->GPU_debug.brief_report) printf("Lattice mesurements: 0x%X-0x%X\n",hlen,(hlen+hlen2));
            hlen += hlen2;
        hlen2 = lattice_energies_size * sizeof(cl_double2);
            if (GPU0->GPU_debug.brief_report) printf("Lattice energies: 0x%X-0x%X\n",hlen,(hlen+hlen2));
            hlen += hlen2;
        if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu)){
            hlen2 = lattice_energies_size_F * sizeof(cl_double2);
            if (GPU0->GPU_debug.brief_report) printf("Lattice plaq av.: 0x%X-0x%X\n",hlen,(hlen+hlen2));
            hlen += hlen2;
        }
        if (get_wilson_loop){
            hlen2 = lattice_energies_size * sizeof(cl_double);
            if (GPU0->GPU_debug.brief_report) printf("Lattice Wilson loop: 0x%X-0x%X\n",hlen,(hlen+hlen2));
            hlen += hlen2;
        }
        if (PL_level > 0){
            hlen2 = lattice_polyakov_loop_size * sizeof(cl_double2);
            if (GPU0->GPU_debug.brief_report) printf("Lattice Polyakov loop: 0x%X-0x%X\n",hlen,(hlen+hlen2));
            hlen += hlen2;
        }
        if (precision == model_precision_single)                                                        // write configuration
            hlen2 = lattice_table_size * sizeof(cl_float4);
        else
            hlen2 = lattice_table_size * sizeof(cl_double4);
        if (GPU0->GPU_debug.brief_report) printf("Lattice data: 0x%X-0x%X\n",hlen,(hlen+hlen2));
        hlen += hlen2;


        if ( fclose(stream) ) printf( "The file was not closed!\n" );
    }
    free(head);
}

unsigned int*   model::lattice_make_bin_header(void){
    int k;
    // bin header structure:
    // 0 - 1 - prefix (8 bytes)
    // 2 - 3 - version
    // 4 - ...
    unsigned int* result = (unsigned int*) calloc(BIN_HEADER_SIZE,sizeof(unsigned int));
    const char* bin_prefix = "QCDGPU";
    k =  0; for(int i=0; i<ceil((double) strlen(bin_prefix)/4); i++) result[k++] = convert_str_uint(bin_prefix,i*4);
    k =  2; for(int i=0; i<ceil((double) strlen(version)/4); i++)    result[k++] = convert_str_uint(version,i*4);
    k =  4;
    result[k++] = INIT;
    result[k++] = convert_start_to_uint(ints);
    result[k++] = PRNG0->PRNG_randseries;
    result[k++] = PRNG0->convert_generator_to_uint(PRNG0->PRNG_generator);
    result[k++] = PRNG0->RL_nskip;
    result[k++] = PRNG0->PRNG_counter;
    result[k++] = NAV;
    result[k++] = NAV_counter;
    result[k++] = NITER;
    result[k++] = ITER;
    result[k++] = ITER_counter;
    result[k++] = NHIT;
    result[k++] = wilson_R;
    result[k++] = wilson_T;
    result[k++] = convert_precision_to_uint(precision);
    result[k++] = GPU0->convert_to_uint_LOW( BETA);
    result[k++] = GPU0->convert_to_uint_HIGH(BETA);
    result[k++] = GPU0->convert_to_uint_LOW( PHI);
    result[k++] = GPU0->convert_to_uint_HIGH(PHI);
    result[k++] = GPU0->convert_to_uint_LOW( OMEGA);
    result[k++] = GPU0->convert_to_uint_HIGH(OMEGA);
    result[k++] = PL_level;
    result[k++] = Fmunu_index1;
    result[k++] = Fmunu_index2;
    result[k++] = get_Fmunu;
    result[k++] = get_actions_avr;
    result[k++] = get_plaquettes_avr;
    result[k++] = get_wilson_loop;
    result[k++] = lattice_measurement_size_F; // measurements initialization
    result[k++] = lattice_energies_size;      // energies initialization
    result[k++] = lattice_energies_size_F;    // energies initialization (with Fmunu tensor, if needed)
    result[k++] = lattice_polyakov_loop_size; // polyakov loop initialization
    result[k++] = lattice_group;
    result[k++] = lattice_nd;
    for (int i=0; i<lattice_nd; i++) result[k++] = lattice_full_size[i];
    for (int i=0; i<lattice_nd; i++) result[k++] = lattice_domain_size[i];

    return result;
}

bool        model::lattice_load_bin_header(unsigned int* head){
    bool result = true;
    int k;
    // bin header structure:
    // 0 - 1 - prefix (8 bytes)
    // 2 - 3 - version
    // 4 - ...
    const char* bin_prefix = "QCDGPU";
    k =  0; for(int i=0; i<ceil((double) strlen(bin_prefix)/4); i++) result &= (head[k++] == convert_str_uint(bin_prefix,i*4));
    k =  2; for(int i=0; i<ceil((double) strlen(version)/4); i++)    result &= (head[k++] == convert_str_uint(version,i*4));
    if (!result) return result;
    k =  4;
      k++;
    ints = convert_uint_to_start(head[k++]);            // 0x14
    PRNG0->PRNG_randseries = head[k++];                 // 0x18
    PRNG0->PRNG_generator = PRNG0->convert_uint_to_generator(head[k++]);    // 0x1C
    PRNG0->RL_nskip = head[k++];                        // 0x20
    PRNG_counter = head[k++];                           // 0x24
    NAV = head[k++];                                    // 0x28
    NAV_counter = head[k++];                            // 0x2C
    NITER = head[k++];                                  // 0x30
    ITER = head[k++];                                   // 0x34
    ITER_counter = head[k++];                           // 0x38
    NHIT = head[k++];                                   // 0x3C
    wilson_R = head[k++];                               // 0x40
    wilson_T = head[k++];                               // 0x44
    precision = convert_uint_to_precision(head[k++]);
    unsigned int get_low  = head[k++];                  // 0x48
    unsigned int get_high = head[k++];                  // 0x4C
    BETA  = GPU0->convert_to_double(get_low,get_high);
    get_low  = head[k++]; get_high = head[k++];         // 0x50, 0x54
    PHI   = GPU0->convert_to_double(get_low,get_high);
    get_low  = head[k++]; get_high = head[k++];         // 0x58, 0x5C
    OMEGA = GPU0->convert_to_double(get_low,get_high);
    PL_level = head[k++];                               // 0x60
    Fmunu_index1 = head[k++];                           // 0x64
    Fmunu_index2 = head[k++];                           // 0x68
    get_Fmunu = (head[k++]==0 ? false : true);          // 0x6C
    get_actions_avr = (head[k++]==0 ? false : true);    // 0x70
    get_plaquettes_avr = (head[k++]==0 ? false : true); // 0x74
    get_wilson_loop = (head[k++]==0 ? false : true);    // 0x78
    lattice_measurement_size_F = head[k++];             // 0x7C - measurements initialization
    lattice_energies_size = head[k++];                  // 0x80 - energies initialization
    lattice_energies_size_F = head[k++];                // 0x84 - energies initialization (with Fmunu tensor, if needed)
    lattice_polyakov_loop_size = head[k++];             // 0x88 - polyakov loop initialization
    lattice_group = head[k++];                          // 0x8C
    lattice_nd = head[k++];                             // 0x90
    for (int i=0; i<lattice_nd; i++) lattice_full_size[i] = head[k++];   // 0x98, 0x9C, 0xA0, 0xA4
    for (int i=0; i<lattice_nd; i++) lattice_domain_size[i] = head[k++]; // 0xA8, 0xAC, 0xB0, 0xB4

    return result;
}

void        model::lattice_load_state(void){
    FILE *stream;
    char buffer[250];
    int j = 0;

    unsigned int* head = (unsigned int*) calloc(BIN_HEADER_SIZE,sizeof(unsigned int));
    bool result = true;

    j  = sprintf_s(buffer  ,sizeof(buffer),  "%s",path);
    j += sprintf_s(buffer+j,sizeof(buffer)-j,"%s",fstate);

    fopen_s(&stream,buffer,"rb");
    if(stream)
    {
        fread(head,sizeof(unsigned int),BIN_HEADER_SIZE,stream);    // load header
        if (LOAD_state==0) result = lattice_load_bin_header(head);
        if (!result) printf("[ERROR in header!!!]\n");
        if (LOAD_state==1) {
            fread(plattice_measurement, sizeof(cl_double2), lattice_measurement_size_F, stream);       // load measurements
            fread(plattice_energies,    sizeof(cl_double2), lattice_energies_size, stream);            // load energies
            if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu))
                fread(plattice_energies_plq,  sizeof(cl_double2), lattice_energies_size_F, stream);    // load energies_plq
            if (get_wilson_loop)
                fread(plattice_wilson_loop,   sizeof(cl_double),  lattice_energies_size, stream);      // load wilson loop
            if (PL_level > 0)
                fread(plattice_polyakov_loop, sizeof(cl_double2), lattice_polyakov_loop_size, stream); // load polyakov loop
            if (precision == model_precision_single)                                                   // load configuration
                fread(plattice_table_float,   sizeof(cl_float4),  lattice_table_size, stream);
            else
                fread(plattice_table_double,  sizeof(cl_double4), lattice_table_size, stream);

            if ( fclose(stream) ) printf( "The file was not closed!\n" );
        }
        LOAD_state++;
    }
    free(head);
}

int getK(int n1, int n2, int ws)
{
  int i;
  
   for(i = n2; i < n2 * ws; i++)
    {
      if(i == ceil((double) n1 * i / ws) * ws / n1) break;
    }
    
  return i;
}
 
#ifdef BIGLAT
void        model::Fmunu_defaults(void)
{
    if(lattice_group == 2)
     {
        Fmunu_index1 = 3;
        if (get_Fmunu1) Fmunu_index1 = 1;
           Fmunu_index2 = 2;
     }
    if(lattice_group == 3)
     {
        Fmunu_index1 = 3;
            if (get_Fmunu1) Fmunu_index1 = 1;
            if (get_Fmunu2) Fmunu_index1 = 2;
            if (get_Fmunu4) Fmunu_index1 = 4;
        Fmunu_index2 = 8;
            if (get_Fmunu5) Fmunu_index2 = 5;
            if (get_Fmunu6) Fmunu_index2 = 6;
            if (get_Fmunu7) Fmunu_index2 = 7;
     }
    
    if ((get_Fmunu)&&(get_F0mu)) get_F0mu = 0;  // only one field (H or E) may be calculated
}
void        model::model_lattice_init(void){
    Fmunu_defaults();

    //size_t workgroup_factor;
    int wln;

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(int k = 0; k < lattice_Nparts; k++){
#endif

        //--- definition of sizes -----
        SubLat[k].sublattice_sites = SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;
        SubLat[k].sublattice_Sites = (SubLat[k].Nx + 2) * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;

        SubLat[k].sublattice_table_row_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_sites);
        SubLat[k].sublattice_table_row_size_half = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_sites / 2);

        SubLat[k].sublattice_table_row_size1 = SubLat[k].GPU0->buffer_size_align(SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt);

        SubLat[k].sublattice_table_row_Size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_Sites);
        SubLat[k].sublattice_table_row_Size_half = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_Sites / 2);

        if(get_wilson_loop){
            SubLat[k].sublattice_table_yzt_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt);
            SubLat[k].sublattice_Lt_rowsize = SubLat[k].GPU0->buffer_size_align((SubLat[k].Nx + 1) * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt);
        }

        SubLat[k].sublattice_table_Size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_table_row_Size * lattice_nd * lattice_group_elements[lattice_group-1] / 4);
        SubLat[k].sublattice_table_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_table_row_size1 * lattice_nd * lattice_group_elements[lattice_group - 1] / 4);

        if(get_wilson_loop){
            SubLat[k].sublattice_Lt_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_Lt_rowsize * lattice_group_elements[lattice_group-1] / 4);
            SubLat[k].sublattice_WLx_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_table_row_size * lattice_group_elements[lattice_group-1] / 4);

            SubLat[k].sublattice_Lr1_size = SubLat[k].sublattice_WLx_size;
            wln = (wilson_R > SubLat[k].Nx) ? SubLat[k].Nx : wilson_R - 1;
            SubLat[k].sublattice_Lr2_size = SubLat[k].GPU0->buffer_size_align(SubLat[k].sublattice_table_yzt_size * wln * lattice_group_elements[lattice_group-1] / 4);
            SubLat[k].sublattice_wlx_size = SubLat[k].sublattice_table_row_size;
        }

        //Parameters
        SubLat[k].sublattice_parameters_size         = SubLat[k].GPU0->buffer_size_align(MODEL_parameter_size);

        //Measurements
        SubLat[k].sublattice_measurement_size        = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil((double) SubLat[k].sublattice_table_row_size / SubLat[k].GPU0->GPU_info.max_workgroup_size)); 
    SubLat[k].sublattice_measurement_size_F      = SubLat[k].sublattice_measurement_size * MODEL_energies_size;

    SubLat[k].iterr                              = SubLat[k].GPU0->buffer_size_align(ITER);
    SubLat[k].sublattice_energies_size           = SubLat[k].GPU0->buffer_size_align(ITER);
    SubLat[k].sublattice_energies_offset         = SubLat[k].sublattice_energies_size;

    if(get_actions_diff){
        SubLat[k].sublattice_action_x_size = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nt));
        SubLat[k].sublattice_action_y_size = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Nx, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nt));
        SubLat[k].sublattice_action_z_size = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Nx, SubLat[k].Ny, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Nz * SubLat[k].Nt));
    }
            SubLat[k].sublattice_action_size = SubLat[k].sublattice_table_row_size;

    SubLat[k].sublattice_polyakov_size           = SubLat[k].GPU0->buffer_size_align((unsigned int) (SubLat[k].Nx * SubLat[k].Ny * SubLat[k].Nz));
    if(PL_level > 2){
        SubLat[k].sublattice_polyakov_x_size           = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Ny));
        SubLat[k].sublattice_polyakov_y_size           = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Nx, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Ny));
        SubLat[k].sublattice_polyakov_z_size           = SubLat[k].GPU0->buffer_size_align((unsigned int) (getK(SubLat[k].Nx, SubLat[k].Ny, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Nz));
    }

    SubLat[k].sublattice_polyakov_loop_size      = SubLat[k].GPU0->buffer_size_align(ITER);      // number of working iterations

        //-----------------------------

        //_____________________________________________ PRNG preparation
        SubLat[k].PRNG0->ndev = k + 1;
        SubLat[k].PRNG0->PRNG_instances   = 0;    // number of instances of generator (or 0 for autoselect)
        // number of samples produced by each generator (quads)
        if (ints == model_start_hot)
            SubLat[k].PRNG0->PRNG_samples     = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil(double(SubLat[k].sublattice_table_row_size * lattice_nd + 3 * SubLat[k].sublattice_table_row_size_half * (NHIT + 1))));
        else
            SubLat[k].PRNG0->PRNG_samples     = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil(double(NHITPar * (3 * SubLat[k].sublattice_table_row_size_half * (NHIT + 1))))); 
        
        SubLat[k].PRNG0->GPU0 = SubLat[k].GPU0;
        SubLat[k].prngstep = SubLat[k].sublattice_table_row_size_half;
    }
        //_____________________________________________ PRNG initialization
    for (int k = 0; k < lattice_Nparts; k++){
        SubLat[k].PRNG0->initialize();
        SubLat[k].GPU0->print_stage("PRNGs initialized");
    }
        //-----------------------------------------------------------------

    char* header = lattice_make_header();
    printf("%s\n",header);
}
#else
void        model::model_lattice_init(void){
    if(lattice_group == 2)
     {
        Fmunu_index1 = 3;
        if (get_Fmunu1) Fmunu_index1 = 1;
           Fmunu_index2 = 2;
     }
    if(lattice_group == 3)
     {
        Fmunu_index1 = 3;
            if (get_Fmunu1) Fmunu_index1 = 1;
            if (get_Fmunu2) Fmunu_index1 = 2;
            if (get_Fmunu4) Fmunu_index1 = 4;
        Fmunu_index2 = 8;
            if (get_Fmunu5) Fmunu_index2 = 5;
            if (get_Fmunu6) Fmunu_index2 = 6;
            if (get_Fmunu7) Fmunu_index2 = 7;
     }
    
    if ((get_Fmunu)&&(get_F0mu)) get_F0mu = 0;  // only one field (H or E) may be calculated

    local_size_intel = (GPU0->GPU_info.device_vendor == GPU0->GPU::GPU_vendor_Intel) ? 64 : 0;
    if (GPU0->GPU_limit_max_workgroup_size) local_size_intel = GPU0->GPU_limit_max_workgroup_size;
    size_t workgroup_factor = (local_size_intel) ? local_size_intel : 32;

    lattice_domain_n1     = lattice_domain_size[0];

    lattice_domain_n1n2   = lattice_domain_n1      * lattice_domain_size[1];
    lattice_domain_n2n3   = lattice_domain_size[1] * lattice_domain_size[2];
    lattice_domain_n1n2n3 = lattice_domain_n1      * lattice_domain_n2n3;
    lattice_domain_exact_n1n2n3 = lattice_domain_size[0] * lattice_domain_size[1] * lattice_domain_size[2];
    lattice_domain_n2n3n4 =  lattice_domain_size[1]    * lattice_domain_size[2] * lattice_domain_size[3];

    lattice_domain_site       = lattice_domain_n1;
    lattice_domain_exact_site = lattice_domain_size[0];
    lattice_full_site         = lattice_full_size[0];
    for (int i=1;i<lattice_nd;i++) {
        lattice_domain_site       *= lattice_domain_size[i];
        lattice_domain_exact_site *= lattice_domain_size[i];
        lattice_full_site         *= lattice_full_size[i];
    }

    lattice_full_n1n2   = lattice_full_size[0] * lattice_full_size[1];
    lattice_full_n2n3   = lattice_full_size[1] * lattice_full_size[2];
    lattice_full_n1n2n3 = lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[2];
    lattice_full_n2n3n4 = lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3];

    lattice_full_link         = lattice_nd * lattice_full_site;
    lattice_domain_link       = lattice_nd * lattice_domain_site;
    lattice_domain_exact_link = lattice_nd * lattice_domain_exact_site;

    lattice_boundary_exact_size = lattice_domain_size[1];    // Size of boundary slice (x=1 in depth)
    for (int i=2;i<lattice_nd;i++) lattice_boundary_exact_size *= lattice_domain_size[i];

    lattice_parameters_size         = GPU0->buffer_size_align(MODEL_parameter_size);
    lattice_energies_size           = GPU0->buffer_size_align(ITER);                      // number of working iterations
    lattice_energies_size_F         = lattice_energies_size * MODEL_energies_size;        // number of working iterations (for tensor Fmunu)
    lattice_energies_offset         = lattice_energies_size;

    lattice_boundary_size           = GPU0->buffer_size_align(lattice_boundary_exact_size);
    lattice_table_row_size          = GPU0->buffer_size_align(lattice_domain_site);
    lattice_table_row_size_half     = GPU0->buffer_size_align(lattice_domain_site / 2);
    lattice_table_exact_row_size    = GPU0->buffer_size_align(lattice_domain_exact_site);
    lattice_table_exact_row_size_half=GPU0->buffer_size_align(lattice_domain_exact_site / 2);
    lattice_table_size              = GPU0->buffer_size_align(lattice_table_row_size * lattice_nd * lattice_group_elements[lattice_group-1] / 4);
    lattice_table_group             = GPU0->buffer_size_align(lattice_table_row_size * lattice_nd);
    lattice_table_exact_group       = GPU0->buffer_size_align(lattice_table_exact_row_size * lattice_nd);

    lattice_measurement_size        = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_table_exact_row_size / workgroup_factor)); // workgroup_factor is the minimum number of workgroup items
    lattice_measurement_size_F      = lattice_measurement_size * MODEL_energies_size;
    lattice_measurement_offset      = lattice_measurement_size;

    if (PL_level>2)
      lattice_polyakov_size         = GPU0->buffer_size_align((unsigned int) (lattice_domain_n2n3 * getK(lattice_domain_n1, lattice_domain_size[1], GPU0->GPU_limit_max_workgroup_size)));
    else
      lattice_polyakov_size           = GPU0->buffer_size_align((unsigned int) lattice_domain_exact_n1n2n3);
    lattice_polyakov_loop_size      = GPU0->buffer_size_align(ITER);      // number of working iterations
    lattice_polyakov_loop_offset    = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_polyakov_size / workgroup_factor));

    if(get_actions_diff)
      lattice_action_size = GPU0->buffer_size_align((unsigned int) (lattice_domain_n2n3n4 * getK(lattice_domain_n1, lattice_domain_size[1], GPU0->GPU_limit_max_workgroup_size)));
    else 
      lattice_action_size = lattice_table_exact_row_size;
    
    if ((!get_Fmunu)&&(!get_F0mu)){
        lattice_energies_size_F    = lattice_energies_size;
        lattice_measurement_size_F = lattice_measurement_size;
    }

    //_____________________________________________ PRNG preparation
        PRNG0->PRNG_instances   = 0;    // number of instances of generator (or 0 for autoselect)
        // number of samples produced by each generator (quads)
        if (ints == model_start_hot)
            PRNG0->PRNG_samples     = GPU0->buffer_size_align((unsigned int) ceil(double(lattice_table_row_size * lattice_nd + 3 * lattice_table_row_size_half * (NHIT + 1))));
        else
            PRNG0->PRNG_samples     = GPU0->buffer_size_align((unsigned int) ceil(double(NHITPar * (3 * lattice_table_row_size_half * (NHIT + 1))))); // 3*(NHIT+1) PRNs per link
        
        PRNG0->GPU0 = GPU0;
        prngstep = lattice_table_row_size_half;
    //-----------------------------------------------------------------

    if (GPU0->GPU_debug.brief_report) {
        printf("Full lattice: -----------------------\n");
        printf("sites                       = %u\n",lattice_full_site);
        printf("n1n2n3                      = %u\n",lattice_full_n1n2n3);
        printf("Domain lattice: ---------------------\n");
        printf("sites                       = %u\n",lattice_domain_site);
        printf("link                        = %u\n",lattice_domain_link);
        printf("n1n2n3                      = %u\n",lattice_domain_n1n2n3);
        printf("sites (exact)               = %u\n",lattice_domain_exact_site);
        printf("links (exact)               = %u\n",lattice_domain_exact_link);
        printf("n1n2n3 (exact)              = %u\n",lattice_domain_exact_n1n2n3);
        printf("lattice_table_exact_row_size= %u\n",lattice_table_exact_row_size);
        printf("..table_exact_row_size_half = %u\n",lattice_table_exact_row_size_half);
        printf("-------------------------------------\n");
        printf("workgroup_size              = %u\n",(unsigned int) (GPU0->GPU_info.max_workgroup_size));
        printf("measurements                = %u\n",lattice_domain_exact_site / (unsigned int) GPU0->GPU_info.max_workgroup_size);
        printf("lattice_table_row_size      = %u\n",lattice_table_row_size);
        printf("lattice_table_row_size_half = %u\n",lattice_table_row_size_half);
        printf("lattice_table_size          = %u\n",lattice_table_size);
        printf("lattice_table_group         = %u\n",lattice_table_group);
        printf("lattice_measurement_size    = %u\n",lattice_measurement_size);
        printf("lattice_measurement_size_F  = %u\n",lattice_measurement_size_F);
        printf("lattice_parameters_size     = %u\n",lattice_parameters_size);
        printf("lattice_energies_size       = %u\n",lattice_energies_size);
        printf("lattice_energies_size_F     = %u\n",lattice_energies_size_F);
        printf("lattice_polyakov_size       = %u\n",lattice_polyakov_size);
        printf("lattice_polyakov_loop_size  = %u\n",lattice_polyakov_loop_size);
        printf("polykov_loop_offset         = %u\n",lattice_polyakov_loop_offset);
        printf("lattice_energies_offset     = %u\n",lattice_energies_offset);
        printf("local_size_intel            = %u\n",(unsigned int)local_size_intel);
        printf("kernels: ----------------------------\n");
        printf("lattice_init                = %u\n",lattice_table_group);
        printf("lattice_GramSchmidt         = %u\n",lattice_table_group);
        printf("lattice_measurement         = %u\n",lattice_table_row_size);
        printf("lattice_measurement_plq     = %u\n",lattice_table_row_size);
        printf("lattice_measurement_wilson  = %u\n",lattice_table_row_size);
        printf("update                      = %u\n",lattice_table_row_size_half);
        printf("lattice_polyakov            = %u\n",lattice_polyakov_size);
        printf("PRNs (in quads): --------------------\n");
        printf("PRNG_instances              = %u\n",PRNG0->PRNG_instances);
        printf("PRNG_samples                = %u\n",PRNG0->PRNG_samples);
        printf("PRNs total                  = %u\n",PRNG0->PRNG_instances * PRNG0->PRNG_samples);
        printf("PRNG_step                   = %u\n",prngstep);
        printf("PRNs for hot start          = %u\n",lattice_table_row_size * 3);
        printf("PRNs for one update         = %u\n",int(ceil(double(3 * lattice_domain_link * (NHIT + 1)))));
        printf("-------------------------------------\n");
        printf(" ROWSIZE                    = %u\n",lattice_table_row_size);
        printf(" NHIT                       = %u\n",NHIT);
        printf(" PRNGSTEP                   = %u\n",lattice_table_row_size_half);
        printf(" PRECISION                  = %u\n",precision);
        printf(" PL                         = %u\n",PL_level);
        printf(" ITER                       = %u\n",ITER);
        if (!((PHI==0.0)&&(OMEGA==0.0))) printf(" TBC\n");     // turn on TBC
        if (get_Fmunu) {
            printf(" FMUNU (%u, %u)\n",Fmunu_index1,Fmunu_index2);   // calculate tensor Fmunu
        }
        if (get_F0mu) {
            printf(" F0MU (%u, %u)\n",Fmunu_index1,Fmunu_index2);   // calculate tensor Fmunu
        }
        printf("-------------------------------------\n");
        printf(" Root path to .CL files     = %s\n",GPU0->cl_root_path);
        printf("-------------------------------------\n");
    }

    //_____________________________________________ PRNG initialization
        PRNG0->initialize();
            GPU0->print_stage("PRNGs initialized");
    //-----------------------------------------------------------------

    char* header = lattice_make_header();
    printf("%s\n",header);
}
#endif

#ifdef BIGLAT
void    model::lattice_mp_Sim(void)
{
    char options_common[1024];
    int options_length_common;

    char options[1024];
    int options_length;

    char buffer_update_cl[FNAME_MAX_LENGTH];
    char* update_source;

    int left_sites = 0;

    int j;

#ifdef USE_OPENMP
#pragma omp parallel for private (options_common, options_length_common, options, options_length, left_sites, buffer_update_cl, update_source, j)
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (int k = 0; k < lattice_Nparts; k++){
#endif
        options_length_common = sprintf_s(options_common, sizeof(options_common), "-Werror");
        if (ints == model_start_gid)
            options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D GID_UPD");
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D SUN=%u", lattice_group);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D ND=%u", lattice_nd);
        if (!((PHI == 0.0) && (OMEGA == 0.0))) options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D TBC");     // turn on TBC
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D PRECISION=%u", precision);
        
#ifndef IGNORE_INTEL
        if (SubLat[k].GPU0->GPU_info.device_vendor == GPU_CL::GPU::GPU_vendor_Intel)
            options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D INTEL_ON");
#endif
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D N1=%u", SubLat[k].Nx + 2);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D N2=%u", SubLat[k].Ny);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D N3=%u", SubLat[k].Nz);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D N4=%u", SubLat[k].Nt);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -I %s%s", SubLat[k].GPU0->cl_root_path, path_suncl);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -I %s%s", SubLat[k].GPU0->cl_root_path, path_kernel);
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D ROWSIZE=%u", SubLat[k].sublattice_table_row_Size);

        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D BIGLAT");

        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D FULL_SITES=%u", lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3]);
        left_sites = 0;
        if (k)
        {
            left_sites = SubLat[0].Nx;
            for (int jj = 1; jj < k; jj++)
                left_sites += SubLat[jj].Nx;
            left_sites *= SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;
        }
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D LEFT_SITES=%u", left_sites);
        
        options_length = sprintf_s(options, sizeof(options), "%s", options_common);
        options_length += sprintf_s(options + options_length, sizeof(options) - options_length, " -D NHIT=%u", NHIT);
        options_length += sprintf_s(options + options_length, sizeof(options) - options_length, " -D NHITPar=%u", NHITPar);
        options_length += sprintf_s(options + options_length, sizeof(options) - options_length, " -D PRNGSTEP=%u", SubLat[k].sublattice_table_row_size_half);

        j = sprintf_s(buffer_update_cl  ,FNAME_MAX_LENGTH,  "%s",SubLat[k].GPU0->cl_root_path);
        j+= sprintf_s(buffer_update_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_UPDATE);
        update_source       = SubLat[k].GPU0->source_read(buffer_update_cl);
                                SubLat[k].GPU0->program_create_ndev(update_source, options, k+1);
    }
}

void    model::lattice_kern_init_Sim(void)
{
    size_t init_global_size[3];

    int argument_id;

#ifdef USE_OPENMP
#pragma omp parallel for private (init_global_size, argument_id)
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (int k = 0; k < lattice_Nparts; k++){
#endif
            init_global_size[0] = SubLat[k].sublattice_table_row_Size;

        init_global_size[1] = 1;
        init_global_size[2] = 1;

        if (ints==model_start_gid) {            // gid init
                SubLat[k].sun_init_id = SubLat[k].GPU0->kernel_init("lattice_init_gid",1,init_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_id,SubLat[k].sublattice_table);
        } else if (ints==model_start_cold) {    // cold init
                SubLat[k].sun_init_id = SubLat[k].GPU0->kernel_init("lattice_init_cold",1,init_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_id,SubLat[k].sublattice_table);
        } else {
                SubLat[k].sun_init_X_id = SubLat[k].GPU0->kernel_init("lattice_init_hot_X", 1, init_global_size, NULL);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_X_id,SubLat[k].sublattice_table);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_X_id,SubLat[k].PRNG0->PRNG_randoms_id);
                SubLat[k].sun_init_Y_id = SubLat[k].GPU0->kernel_init("lattice_init_hot_Y", 1, init_global_size, NULL);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_Y_id,SubLat[k].sublattice_table);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_Y_id,SubLat[k].PRNG0->PRNG_randoms_id);
                SubLat[k].sun_init_Z_id = SubLat[k].GPU0->kernel_init("lattice_init_hot_Z", 1, init_global_size, NULL);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_Z_id,SubLat[k].sublattice_table);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_Z_id,SubLat[k].PRNG0->PRNG_randoms_id);
                SubLat[k].sun_init_T_id = SubLat[k].GPU0->kernel_init("lattice_init_hot_T", 1, init_global_size, NULL);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_T_id,SubLat[k].sublattice_table);
                    argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_init_T_id,SubLat[k].PRNG0->PRNG_randoms_id);
        }

        SubLat[k].sun_GramSchmidt_id = SubLat[k].GPU0->kernel_init("lattice_GramSchmidt",1,init_global_size,NULL); 
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_GramSchmidt_id, SubLat[k].sublattice_table);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_GramSchmidt_id, SubLat[k].sublattice_parameters);
    }
}

void    model::lattice_kern_update_Sim(void)
{
    size_t monte_global_size[3], monte_local_size[3];

    int argument_id;

#ifdef USE_OPENMP
#pragma omp parallel for private (monte_global_size, monte_local_size, argument_id)
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (int k = 0; k < lattice_Nparts; k++){
#endif
        monte_global_size[0] = SubLat[k].sublattice_table_row_size_half;
        monte_global_size[1] = 1;
        monte_global_size[2] = 1;

        monte_local_size[0] = SubLat[k].GPU0->GPU_info.max_workgroup_size;
        monte_local_size[1] = 1;
        monte_local_size[2] = 1;

        SubLat[k].sun_update_odd_X_id = SubLat[k].GPU0->kernel_init("update_odd_X",1,monte_global_size, monte_local_size);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_X_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_X_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_X_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_even_X_id = SubLat[k].GPU0->kernel_init("update_even_X",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_X_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_X_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_X_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_odd_Y_id = SubLat[k].GPU0->kernel_init("update_odd_Y",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Y_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Y_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Y_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_even_Y_id = SubLat[k].GPU0->kernel_init("update_even_Y",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Y_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Y_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Y_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_odd_Z_id = SubLat[k].GPU0->kernel_init("update_odd_Z",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Z_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Z_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_Z_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_even_Z_id = SubLat[k].GPU0->kernel_init("update_even_Z",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Z_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Z_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_Z_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_odd_T_id = SubLat[k].GPU0->kernel_init("update_odd_T",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_T_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_T_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_odd_T_id,SubLat[k].PRNG0->PRNG_randoms_id);

        SubLat[k].sun_update_even_T_id = SubLat[k].GPU0->kernel_init("update_even_T",1,monte_global_size,NULL);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_T_id,SubLat[k].sublattice_table);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_T_id,SubLat[k].sublattice_parameters);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_update_even_T_id,SubLat[k].PRNG0->PRNG_randoms_id);
    }
}

void    model::lattice_mp_Meas(void)
{
    char options_common[1024];
    int options_length_common;

    char options_measurements[1024];
    int options_measurement_length;

    char buffer_measurements_cl[FNAME_MAX_LENGTH];
    char* measurements_source;

    int left_sites = 0;

    int j;

#ifdef USE_OPENMP
#pragma omp parallel for private (options_common, options_length_common, options_measurements, options_measurement_length, left_sites, buffer_measurements_cl, measurements_source, j)
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (int k = 0; k < lattice_Nparts; k++){
#endif
        options_length_common  = sprintf_s(options_common,sizeof(options_common),"-Werror");
        if(ints==model_start_gid)
            options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D GID_UPD");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D SUN=%u",         lattice_group);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ND=%u",          lattice_nd);
        if (!((PHI==0.0)&&(OMEGA==0.0))) options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D TBC");     // turn on TBC
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRECISION=%u", precision);   
    
#ifndef IGNORE_INTEL
        if (SubLat[k].GPU0->GPU_info.device_vendor == GPU_CL::GPU::GPU_vendor_Intel)
            options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D INTEL_ON");
#endif
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N1=%u", SubLat[k].Nx);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N2=%u", SubLat[k].Ny);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N3=%u", SubLat[k].Nz);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N4=%u", SubLat[k].Nt);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_suncl);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_kernel);
#ifdef BIGTOSMALL
        options_length_common += sprintf_s(options_common + options_length_common, sizeof(options_common) - options_length_common, " -D ROWSIZE=%u", SubLat[k].sublattice_table_row_size1);
#else
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE=%u", SubLat[k].sublattice_table_row_Size);
#endif
    if(get_actions_diff){
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLK=%u",   getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKx=%u",   getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKy=%u",   getK(SubLat[k].Nx, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKz=%u",   getK(SubLat[k].Nx, SubLat[k].Ny, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
    }

        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D BIGLAT");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D FULL_SITES=%u", lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3]);
        left_sites = 0;
        if (k)
        {
            left_sites = SubLat[0].Nx;
            for (int j = 1; j < k; j++)
                left_sites += SubLat[j].Nx;
            left_sites *= SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;
        }
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D LEFT_SITES=%u", left_sites);

        options_measurement_length  = sprintf_s(options_measurements,sizeof(options_measurements),"%s",options_common);

    if (get_Fmunu) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU");   // calculate tensor Fmunu for H field
    if (get_F0mu)  options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D F0MU");   // calculate tensor Fmunu for E field

    if ((get_Fmunu)||(get_F0mu)) {
        if (get_Fmunu1) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU1");   // calculate tensor Fmunu for lambda1 matrix
        if (get_Fmunu2) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU2");   // calculate tensor Fmunu for lambda2 matrix
        if (get_Fmunu4) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU4");   // calculate tensor Fmunu for lambda4 matrix
        if (get_Fmunu5) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU5");   // calculate tensor Fmunu for lambda5 matrix
        if (get_Fmunu6) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU6");   // calculate tensor Fmunu for lambda6 matrix
        if (get_Fmunu7) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU7");   // calculate tensor Fmunu for lambda7 matrix
    }
    
        j = sprintf_s(buffer_measurements_cl  ,FNAME_MAX_LENGTH,  "%s",SubLat[k].GPU0->cl_root_path);
        j+= sprintf_s(buffer_measurements_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_MEASUREMENTS);
        measurements_source       = SubLat[k].GPU0->source_read(buffer_measurements_cl);
                                    SubLat[k].GPU0->program_create_ndev(measurements_source,options_measurements, k+1);
    }
}

void    model::lattice_kern_init_Plq(void)
{
    cl_uint4 mesurement_plq_param;

    int size_reduce_measurement_plq_double2   = 0;
    int offset_reduce_measurement_plq_double2 = 0;

    size_t measurement3_global_size[3];
    size_t local_size_lattice_measurement[3];
    size_t reduce_measurement_global_size[3];
    size_t reduce_local_size[3];

    int mwgs;

    int argument_id;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        SubLat[k].sun_measurement_plq_id        = 0;
        SubLat[k].sun_measurement_plq_reduce_id = 0;

        measurement3_global_size[0] = SubLat[k].sublattice_action_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

        mwgs = SubLat[k].GPU0->GPU_info.max_workgroup_size;

        local_size_lattice_measurement[0] = mwgs;
        local_size_lattice_measurement[1] = 1;
        local_size_lattice_measurement[2] = 1;

        reduce_measurement_global_size[0] = mwgs;
        reduce_measurement_global_size[1] = 1;
        reduce_measurement_global_size[2] = 1;

        reduce_local_size[0] = mwgs;
        reduce_local_size[1] = 1;
        reduce_local_size[2] = 1;

        if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) {
            SubLat[k].sun_measurement_plq_id = SubLat[k].GPU0->kernel_init("lattice_measurement_plq",1,measurement3_global_size,local_size_lattice_measurement);
            offset_reduce_measurement_plq_double2 = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil((double) SubLat[k].sublattice_table_row_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_measurement_plq_id)),SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_measurement_plq_id));
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_id,SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_id,SubLat[k].sublattice_measurement);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_id,SubLat[k].sublattice_parameters);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_id, SubLat[k].sublattice_lds);
            argument_id = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_measurement_plq_id, &offset_reduce_measurement_plq_double2);
            printf("offset_reduce_measurement_plq_double2 = %i\n", offset_reduce_measurement_plq_double2);

        size_reduce_measurement_plq_double2 = (int) ceil((double) SubLat[k].sublattice_table_row_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_measurement_plq_id));
        SubLat[k].sun_measurement_plq_reduce_id = SubLat[k].GPU0->kernel_init("reduce_measurement_plq_double2", 1, reduce_measurement_global_size, reduce_local_size);
                          argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_reduce_id, SubLat[k].sublattice_measurement);
                          argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_reduce_id, SubLat[k].sublattice_plq);
                          argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_plq_reduce_id, SubLat[k].sublattice_lds);
            mesurement_plq_param.s[0] = size_reduce_measurement_plq_double2;
            mesurement_plq_param.s[1] = offset_reduce_measurement_plq_double2;
            mesurement_plq_param.s[2] = SubLat[k].sublattice_energies_offset;
            mesurement_plq_param.s[3] = 0;
                          SubLat[k].argument_plq_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_measurement_plq_reduce_id,&mesurement_plq_param);
        }
    }

}

void    model::lattice_kern_init_Action(void)
{
    cl_uint4 mesurement_plq_param;

    int size_reduce_measurement_double2   = 0;

    size_t measurement3_global_size[3];
    size_t local_size_lattice_measurement[3];
    size_t reduce_measurement_global_size[3];
    size_t reduce_local_size[3];

    int mwgs;

    int argument_id;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        SubLat[k].sun_measurement_id        = 0;
        SubLat[k].sun_measurement_reduce_id = 0;

        measurement3_global_size[0] = SubLat[k].sublattice_action_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

        mwgs = SubLat[k].GPU0->GPU_info.max_workgroup_size;
        printf("mwgs = %i\n", mwgs);

        local_size_lattice_measurement[0] = mwgs;
        local_size_lattice_measurement[1] = 1;
        local_size_lattice_measurement[2] = 1;

        reduce_measurement_global_size[0] = mwgs;
        reduce_measurement_global_size[1] = 1;
        reduce_measurement_global_size[2] = 1;

        reduce_local_size[0] = mwgs;
        reduce_local_size[1] = 1;
        reduce_local_size[2] = 1;

        if (get_actions_avr) {
            SubLat[k].sun_measurement_id = SubLat[k].GPU0->kernel_init("lattice_measurement",1,measurement3_global_size,local_size_lattice_measurement);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_id,SubLat[k].sublattice_table);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_id,SubLat[k].sublattice_measurement);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_id,SubLat[k].sublattice_parameters);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_id,SubLat[k].sublattice_lds);

           size_reduce_measurement_double2 = (int) ceil((double) SubLat[k].sublattice_table_row_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_measurement_id));

           SubLat[k].sun_measurement_reduce_id = SubLat[k].GPU0->kernel_init("reduce_measurement_double2",1,reduce_measurement_global_size,reduce_local_size);
                  argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_reduce_id,SubLat[k].sublattice_measurement);
                  argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_reduce_id, SubLat[k].sublattice_energies);
                  argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_reduce_id,SubLat[k].sublattice_lds);
                  SubLat[k].argument_measurement_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_measurement_reduce_id,&size_reduce_measurement_double2);
        }
    }

}

void    model::lattice_kern_init_Action_diff(void){
    cl_uint4 Sparam;
    int argument_id;
    int mwgs;

    size_t measurement3_global_size[3];
    size_t local_size_lattice_measurement[3];
    size_t reduce_measurement_global_size[3];
    size_t reduce_local_size[3];

    int size_reduce_action_diff_double2;

    for (int k = 0; k < lattice_Nparts; k++){
        mwgs = SubLat[k].GPU0->GPU_info.max_workgroup_size;

        local_size_lattice_measurement[0] = mwgs;
        local_size_lattice_measurement[1] = 1;
        local_size_lattice_measurement[2] = 1;

        reduce_measurement_global_size[0] = mwgs;
        reduce_measurement_global_size[1] = 1;
        reduce_measurement_global_size[2] = 1;

        reduce_local_size[0] = mwgs;
        reduce_local_size[1] = 1;
        reduce_local_size[2] = 1;
      
        measurement3_global_size[0] = SubLat[k].sublattice_action_x_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

    SubLat[k].sun_action_diff_x_id = SubLat[k].GPU0->kernel_init("lattice_action_diff_x", 1, measurement3_global_size, local_size_lattice_measurement);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_id, SubLat[k].sublattice_table);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_id, SubLat[k].sublattice_parameters);	
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_id, SubLat[k].sublattice_lds);
        size_reduce_action_diff_double2 = (int) ceil((double) SubLat[k].sublattice_action_x_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_action_diff_x_id));

        Sparam.s[0] = (int) ceil((double) SubLat[k].sublattice_action_x_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_action_diff_x_id));
        Sparam.s[1] = SubLat[k].sublattice_energies_size;
        Sparam.s[2] = 0;
        Sparam.s[3] = 0;
    
        SubLat[k].sun_action_diff_x_reduce_id = SubLat[k].GPU0->kernel_init("reduce_action_diff_x_double2", 1, reduce_measurement_global_size, reduce_local_size);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_reduce_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_reduce_id, SubLat[k].sublattice_action_diff_x);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_x_reduce_id, SubLat[k].sublattice_lds);
           SubLat[k].argument_action_diff_x_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_action_diff_x_reduce_id, &Sparam);
      
        measurement3_global_size[0] = SubLat[k].sublattice_action_y_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

        SubLat[k].sun_action_diff_y_id = SubLat[k].GPU0->kernel_init("lattice_action_diff_y", 1, measurement3_global_size, local_size_lattice_measurement);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_id, SubLat[k].sublattice_table);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_id, SubLat[k].sublattice_parameters);	
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_id, SubLat[k].sublattice_lds);
        size_reduce_action_diff_double2 = (int) ceil((double) SubLat[k].sublattice_action_y_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_action_diff_y_id));

            Sparam.s[0] = size_reduce_action_diff_double2;
        Sparam.s[1] = SubLat[k].sublattice_energies_size;
        Sparam.s[2] = 0;
        Sparam.s[3] = 0;
    
        SubLat[k].sun_action_diff_y_reduce_id = SubLat[k].GPU0->kernel_init("reduce_action_diff_y_double2", 1, reduce_measurement_global_size, reduce_local_size);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_reduce_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_reduce_id, SubLat[k].sublattice_action_diff_y);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_y_reduce_id, SubLat[k].sublattice_lds);
           SubLat[k].argument_action_diff_y_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_action_diff_y_reduce_id, &Sparam);
      
        measurement3_global_size[0] = SubLat[k].sublattice_action_z_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

        SubLat[k].sun_action_diff_z_id = SubLat[k].GPU0->kernel_init("lattice_action_diff_z", 1, measurement3_global_size, local_size_lattice_measurement);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_id, SubLat[k].sublattice_table);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_id, SubLat[k].sublattice_parameters);	
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_id, SubLat[k].sublattice_lds);
        size_reduce_action_diff_double2 = (int) ceil((double) SubLat[k].sublattice_action_z_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_action_diff_z_id));

        Sparam.s[0] = (int) ceil((double) SubLat[k].sublattice_action_z_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_action_diff_z_id));
        Sparam.s[1] = SubLat[k].sublattice_energies_size;
        Sparam.s[2] = 0;
        Sparam.s[3] = 0;
    
        SubLat[k].sun_action_diff_z_reduce_id = SubLat[k].GPU0->kernel_init("reduce_action_diff_z_double2", 1, reduce_measurement_global_size, reduce_local_size);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_reduce_id, SubLat[k].sublattice_measurement_diff);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_reduce_id, SubLat[k].sublattice_action_diff_z);
           argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_action_diff_z_reduce_id, SubLat[k].sublattice_lds);
           SubLat[k].argument_action_diff_z_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_action_diff_z_reduce_id, &Sparam);
    }
}

void    model::lattice_mp_PL(void)
{
    char options_common[1024];
    int options_length_common;

    char options_polyakov[1024];
    int options_length_polyakov;

    char buffer_polyakov_cl[FNAME_MAX_LENGTH];
    char* polyakov_source;

    int left_sites = 0;
    int j;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        options_length_common  = sprintf_s(options_common,sizeof(options_common),"-Werror");
        if(ints==model_start_gid)
            options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D GID_UPD");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D SUN=%u",         lattice_group);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ND=%u",          lattice_nd);
        if (!((PHI==0.0)&&(OMEGA==0.0))) options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D TBC");     // turn on TBC
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRECISION=%u", precision);   
    
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N1=%u", SubLat[k].Nx);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N2=%u", SubLat[k].Ny);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N3=%u", SubLat[k].Nz);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N4=%u", SubLat[k].Nt);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_suncl);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_kernel);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE=%u", SubLat[k].sublattice_table_row_Size);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLK=%u",   getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKx=%u",   getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKy=%u",   getK(SubLat[k].Nx, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size));
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLKz=%u",   getK(SubLat[k].Nx, SubLat[k].Ny, SubLat[k].GPU0->GPU_limit_max_workgroup_size));

        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D BIGLAT");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D FULL_SITES=%u", lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3]);
        if (k)
        {
            left_sites = SubLat[0].Nx;
            for (int j = 1; j < k; j++)
                left_sites += SubLat[j].Nx;
            left_sites *= SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;
        }
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D LEFT_SITES=%u", left_sites);

        options_length_polyakov  = sprintf_s(options_polyakov,sizeof(options_polyakov),"%s",options_common);
        options_length_polyakov += sprintf_s(options_polyakov + options_length_polyakov,sizeof(options_polyakov)-options_length_polyakov," -D PL=%u", PL_level);

        j = sprintf_s(buffer_polyakov_cl  ,FNAME_MAX_LENGTH,  "%s", SubLat[k].GPU0->cl_root_path);
        j+= sprintf_s(buffer_polyakov_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_POLYAKOV);
        polyakov_source = SubLat[k].GPU0->source_read(buffer_polyakov_cl);
                          SubLat[k].GPU0->program_create_ndev(polyakov_source,options_polyakov, k + 1);
    }
}

void    model::lattice_kern_init_PL(void)
{
    int size_reduce_polyakov_double2   = 0;
    int offset_reduce_polyakov_double2 = 0;

    int size_reduce_polyakov_diff_double2   = 0;
    int offset_reduce_polyakov_diff_double2 = 0;

    int argument_id;

    cl_uint4 polyakov_param;

    size_t polyakov3_global_size[3];
    size_t local_size_lattice_polyakov[3];
    size_t reduce_polyakov_global_size[3];
    size_t reduce_local_size[3];

    int mwgs;

    for(int k = 0; k < lattice_Nparts; k++)
    {
        SubLat[k].sun_polyakov_id        = 0;
        SubLat[k].sun_polyakov_reduce_id = 0;

        polyakov3_global_size[0] = SubLat[k].sublattice_polyakov_size;
        polyakov3_global_size[1] = 1;
        polyakov3_global_size[2] = 1;

        mwgs = SubLat[k].GPU0->GPU_info.max_workgroup_size;

        local_size_lattice_polyakov[0] = mwgs;
        local_size_lattice_polyakov[1] = 1;
        local_size_lattice_polyakov[2] = 1;

        reduce_polyakov_global_size[0] = mwgs;
        reduce_polyakov_global_size[1] = 1;
        reduce_polyakov_global_size[2] = 1;

        reduce_local_size[0] = mwgs;
        reduce_local_size[1] = 1;
        reduce_local_size[2] = 1;

        SubLat[k].sun_polyakov_id = SubLat[k].GPU0->kernel_init("lattice_polyakov", 1, polyakov3_global_size, local_size_lattice_polyakov);
        offset_reduce_polyakov_double2 = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil((double) SubLat[k].sublattice_polyakov_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_id)), SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_id));
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_id, SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_id, SubLat[k].sublattice_measurement);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_id, SubLat[k].sublattice_parameters);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_id, SubLat[k].sublattice_lds);
            argument_id = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_id,&offset_reduce_polyakov_double2);
        size_reduce_polyakov_double2 = (int) ceil((double) SubLat[k].sublattice_polyakov_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_id));

        SubLat[k].sun_polyakov_reduce_id = SubLat[k].GPU0->kernel_init("reduce_polyakov_double2", 1, reduce_polyakov_global_size, reduce_local_size);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_reduce_id, SubLat[k].sublattice_measurement);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_reduce_id, SubLat[k].sublattice_polyakov_loop);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_reduce_id, SubLat[k].sublattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_double2;
            polyakov_param.s[2] = SubLat[k].iterr;
            polyakov_param.s[3] = 0;
        SubLat[k].argument_polyakov_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_reduce_id, &polyakov_param);

        //-----------------------------------------

        if (PL_level > 2) {
            polyakov3_global_size[0] = SubLat[k].sublattice_polyakov_x_size;
            polyakov3_global_size[1] = 1;
            polyakov3_global_size[2] = 1;

            SubLat[k].sun_polyakov_diff_x_id = SubLat[k].GPU0->kernel_init("lattice_polyakov_diff_x", 1, polyakov3_global_size, local_size_lattice_polyakov);
            offset_reduce_polyakov_diff_double2 = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil((double) SubLat[k].sublattice_polyakov_x_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_x_id)),SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_x_id));
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_id, SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_id, SubLat[k].sublattice_parameters);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_id, SubLat[k].sublattice_lds);
            argument_id = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_x_id, &offset_reduce_polyakov_diff_double2);
        size_reduce_polyakov_diff_double2 = (int) ceil((double) SubLat[k].sublattice_polyakov_x_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_x_id));

            SubLat[k].sun_polyakov_diff_x_reduce_id = SubLat[k].GPU0->kernel_init("reduce_polyakov_diff_x_double2", 1, reduce_polyakov_global_size, reduce_local_size); 

            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_reduce_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_reduce_id, SubLat[k].sublattice_polyakov_loop_diff_x);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_x_reduce_id, SubLat[k].sublattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = SubLat[k].sublattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            SubLat[k].argument_polyakov_diff_x_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_x_reduce_id, &polyakov_param);
    
            polyakov3_global_size[0] = SubLat[k].sublattice_polyakov_y_size;
            polyakov3_global_size[1] = 1;
            polyakov3_global_size[2] = 1;

        SubLat[k].sun_polyakov_diff_y_id = SubLat[k].GPU0->kernel_init("lattice_polyakov_diff_y", 1, polyakov3_global_size, local_size_lattice_polyakov);
        offset_reduce_polyakov_diff_double2 = 0;
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_id, SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_id, SubLat[k].sublattice_parameters);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_id, SubLat[k].sublattice_lds);
            argument_id = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_y_id, &offset_reduce_polyakov_diff_double2);
        size_reduce_polyakov_diff_double2 = (int) ceil((double) SubLat[k].sublattice_polyakov_y_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_y_id));

            SubLat[k].sun_polyakov_diff_y_reduce_id = SubLat[k].GPU0->kernel_init("reduce_polyakov_diff_y_double2", 1, reduce_polyakov_global_size, reduce_local_size); 
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_reduce_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_reduce_id, SubLat[k].sublattice_polyakov_loop_diff_y);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_y_reduce_id, SubLat[k].sublattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = SubLat[k].sublattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            SubLat[k].argument_polyakov_diff_y_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_y_reduce_id, &polyakov_param);
        
    
            polyakov3_global_size[0] = SubLat[k].sublattice_polyakov_z_size;
            polyakov3_global_size[1] = 1;
            polyakov3_global_size[2] = 1;

            SubLat[k].sun_polyakov_diff_z_id = SubLat[k].GPU0->kernel_init("lattice_polyakov_diff_z", 1, polyakov3_global_size, local_size_lattice_polyakov);
            offset_reduce_polyakov_diff_double2 = SubLat[k].GPU0->buffer_size_align((unsigned int) ceil((double) SubLat[k].sublattice_polyakov_z_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_z_id)), SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_z_id));
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_id, SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_id, SubLat[k].sublattice_parameters);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_id, SubLat[k].sublattice_lds);
            argument_id = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_z_id, &offset_reduce_polyakov_diff_double2);
            size_reduce_polyakov_diff_double2 = (int) ceil((double) SubLat[k].sublattice_polyakov_z_size / SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_polyakov_diff_z_id));

            SubLat[k].sun_polyakov_diff_z_reduce_id = SubLat[k].GPU0->kernel_init("reduce_polyakov_diff_z_double2", 1, reduce_polyakov_global_size, reduce_local_size); 
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_reduce_id, SubLat[k].sublattice_measurement_diff);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_reduce_id, SubLat[k].sublattice_polyakov_loop_diff_z);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_polyakov_diff_z_reduce_id, SubLat[k].sublattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = SubLat[k].sublattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            SubLat[k].argument_polyakov_diff_z_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_polyakov_diff_z_reduce_id, &polyakov_param);
        }
    }
}

void    model::lattice_mp_WL(void)
{
    char options_common[1024];
    int options_length_common;

    char options_wilson[1024];
    int options_length_wilson;

    char buffer_wilson_cl[FNAME_MAX_LENGTH];
    char* wilson_source;

    int left_sites = 0;
    int j;

    int wln;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        options_length_common  = sprintf_s(options_common,sizeof(options_common),"-Werror");
        if(ints==model_start_gid)
            options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D GID_UPD");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D SUN=%u",         lattice_group);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ND=%u",          lattice_nd);
        if (!((PHI==0.0)&&(OMEGA==0.0))) options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D TBC");     // turn on TBC
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRECISION=%u", precision);   
    
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N1=%u", SubLat[k].Nx);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N2=%u", SubLat[k].Ny);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N3=%u", SubLat[k].Nz);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N4=%u", SubLat[k].Nt);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_suncl);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s", SubLat[k].GPU0->cl_root_path,path_kernel);

        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE=%u", SubLat[k].sublattice_table_row_Size);

        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE_LT=%u", SubLat[k].sublattice_Lt_rowsize);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE_LR2=%u", SubLat[k].sublattice_table_yzt_size);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE_WLX=%u", SubLat[k].sublattice_table_row_size);

        if (PRNG0->PRNG_precision == PRNG_CL::PRNG::PRNG_precision_double)
            options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRNG_PRECISION=2");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D BIGLAT");
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D FULL_SITES=%u", lattice_full_size[0] * lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3]);
        if (k)
        {
            left_sites = SubLat[0].Nx;
            for (int j = 1; j < k; j++)
                left_sites += SubLat[j].Nx;
            SubLat[k].left_x = left_sites;
            left_sites *= SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt;
        }
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D LEFT_SITES=%u", left_sites);
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D K=%u", k);

        options_length_wilson  = sprintf_s(options_wilson,sizeof(options_wilson),"%s", options_common);
        options_length_wilson += sprintf_s(options_wilson + options_length_wilson, sizeof(options_wilson) - options_length_wilson," -D WLR=%u", wilson_R);
        options_length_wilson += sprintf_s(options_wilson + options_length_wilson,sizeof(options_wilson) - options_length_wilson," -D WLT=%u", wilson_T);
        wln = (wilson_R > SubLat[k].Nx) ? SubLat[k].Nx : wilson_R - 1;
        options_length_wilson += sprintf_s(options_wilson + options_length_wilson,sizeof(options_wilson) - options_length_wilson," -D WLN=%u", wln);

        j = sprintf_s(buffer_wilson_cl  ,FNAME_MAX_LENGTH,  "%s", SubLat[k].GPU0->cl_root_path);
        j+= sprintf_s(buffer_wilson_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_WILSON_LOOP);
        wilson_source = SubLat[k].GPU0->source_read(buffer_wilson_cl);
                          SubLat[k].GPU0->program_create_ndev(wilson_source, options_wilson, k + 1);
    }
}

void    model::lattice_kern_init_WL(void)
{
    size_t measurement3_global_size[3];
    size_t measurement3_Lt_global_size[3];
    size_t measurement3_wl_global_size[3];
    size_t local_size_lattice_measurement[3];
    size_t measurementLr2_global_size[3];
    size_t reduce_measurement_global_size[3];
    size_t reduce_local_size[3];

    int mwgs;

    int argument_id;

    int size_reduce_wilson_double2 = 0;

    for (int k = 0; k < lattice_Nparts; k++)
    {
        measurement3_global_size[0] = SubLat[k].sublattice_Lt_size;
        measurement3_global_size[1] = 1;
        measurement3_global_size[2] = 1;

        measurement3_Lt_global_size[0] = SubLat[k].GPU0->buffer_size_align((SubLat[k].Nx + 1) * SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt);;
        measurement3_Lt_global_size[1] = 1;
        measurement3_Lt_global_size[2] = 1;

        mwgs = SubLat[k].GPU0->GPU_info.max_workgroup_size;

        local_size_lattice_measurement[0] = mwgs;
        local_size_lattice_measurement[1] = 1;
        local_size_lattice_measurement[2] = 1;

        measurementLr2_global_size[0] = SubLat[k].GPU0->buffer_size_align(SubLat[k].Ny * SubLat[k].Nz * SubLat[k].Nt);
        measurementLr2_global_size[1] = 1;
        measurementLr2_global_size[2] = 1;

        measurement3_wl_global_size[0] = SubLat[k].sublattice_wlx_size;
        measurement3_wl_global_size[1] = 1;
        measurement3_wl_global_size[2] = 1;

        reduce_measurement_global_size[0] = mwgs;
        reduce_measurement_global_size[1] = 1;
        reduce_measurement_global_size[2] = 1;

        reduce_local_size[0] = mwgs;
        reduce_local_size[1] = 1;
        reduce_local_size[2] = 1;

        SubLat[k].sun_measurement_Lt_id = 0;
        SubLat[k].sun_measurement_Lt_id = SubLat[k].GPU0->kernel_init("lattice_measurement_Lt", 1, measurement3_Lt_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lt_id, SubLat[k].sublattice_table);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lt_id, SubLat[k].sublattice_Lt);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lt_id, SubLat[k].sublattice_parameters);

        SubLat[k].sun_measurement_Lr1_id = 0;
        SubLat[k].sun_measurement_Lr1_id = SubLat[k].GPU0->kernel_init("lattice_measurement_Lr1", 1, measurement3_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr1_id, SubLat[k].sublattice_table);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr1_id, SubLat[k].sublattice_Lr1);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr1_id, SubLat[k].sublattice_parameters);

        if(SubLat[k].size_sublattice_Lr2){
            SubLat[k].sun_measurement_Lr2_id = 0;
            SubLat[k].sun_measurement_Lr2_id = SubLat[k].GPU0->kernel_init("lattice_measurement_Lr2", 1, measurementLr2_global_size, local_size_lattice_measurement);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr2_id, SubLat[k].sublattice_table);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr2_id, SubLat[k].sublattice_Lr2);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_Lr2_id, SubLat[k].sublattice_parameters);
        }

        SubLat[k].sun_measurement_WLx0_id = 0;
        SubLat[k].sun_measurement_WLx0_id = SubLat[k].GPU0->kernel_init("lattice_measurement_WLx0", 1, measurement3_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx0_id, SubLat[k].sublattice_Lt);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx0_id, SubLat[k].sublattice_Lr1);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx0_id, SubLat[k].sublattice_WL_x);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx0_id, SubLat[k].sublattice_parameters);

        SubLat[k].sun_measurement_WLx1_id = 0;
        SubLat[k].sun_measurement_WLx1_id = SubLat[k].GPU0->kernel_init("lattice_measurement_WLx1", 1, measurement3_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx1_id, SubLat[k].sublattice_WL_x);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx1_id, SubLat[k].sublattice_Lt);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx1_id, SubLat[k].sublattice_m);

        SubLat[k].sun_measurement_WLx2_id = 0;
        SubLat[k].sun_measurement_WLx2_id = SubLat[k].GPU0->kernel_init("lattice_measurement_WLx2", 1, measurement3_global_size, local_size_lattice_measurement);

        SubLat[k].sun_measurement_WLx3_id = 0;
        SubLat[k].sun_measurement_WLx3_id = SubLat[k].GPU0->kernel_init("lattice_measurement_WLx3", 1, measurement3_global_size, local_size_lattice_measurement);

        SubLat[k].sun_measurement_WLx4_id = 0;
        SubLat[k].sun_measurement_WLx4_id = SubLat[k].GPU0->kernel_init("lattice_measurement_WLx4", 1, measurement3_wl_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx4_id, SubLat[k].sublattice_WL_x);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx4_id, SubLat[k].sublattice_wlx);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx4_id, SubLat[k].sublattice_lds);

        SubLat[k].sun_measurement_wilson_id = 0;
        SubLat[k].sun_measurement_wilson_id = SubLat[k].GPU0->kernel_init("lattice_measurement_wilson", 1, measurement3_wl_global_size, local_size_lattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_wilson_id, SubLat[k].sublattice_table);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_wilson_id, SubLat[k].sublattice_wlx);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_wilson_id, SubLat[k].sublattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_wilson_id, SubLat[k].sublattice_parameters);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_wilson_id, SubLat[k].sublattice_lds);

        size_reduce_wilson_double2 = (int) ceil((double) SubLat[k].sublattice_wlx_size/ SubLat[k].GPU0->kernel_get_worksize(SubLat[k].sun_measurement_wilson_id));

        SubLat[k].sun_wilson_loop_reduce_id = SubLat[k].GPU0->kernel_init("reduce_wilson_double2", 1, reduce_measurement_global_size, reduce_local_size);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_wilson_loop_reduce_id, SubLat[k].sublattice_measurement);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_wilson_loop_reduce_id, SubLat[k].sublattice_wilson_loop);
        argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_wilson_loop_reduce_id, SubLat[k].sublattice_lds);
        SubLat[k].argument_wilson_index = SubLat[k].GPU0->kernel_init_constant(SubLat[k].sun_wilson_loop_reduce_id,&size_reduce_wilson_double2);
    }

}

void        model::lattice_make_programs(void)
{
    lattice_mp_Sim();
    lattice_kern_init_Sim();
    lattice_kern_update_Sim();
    
    lattice_mp_Meas();
    lattice_kern_init_Plq();
    lattice_kern_init_Action();
    
    if(get_actions_diff){
        lattice_kern_init_Action_diff();
    }

    if (PL_level > 0){
        lattice_mp_PL();
        lattice_kern_init_PL();
    }
    
    if(get_wilson_loop){
        lattice_mp_WL();
        lattice_kern_init_WL();
    }

}
#else
void        model::lattice_make_programs(void)
{
    int j;
    int argument_id;
    wilson_index      = 0;
    plq_index         = 0;
    polyakov_index    = 0;
    measurement_index = 0;

    char options_common[1024];
    int options_length_common  = sprintf_s(options_common,sizeof(options_common),"-Werror");
    if (GPU0->GPU_info.device_vendor == GPU_CL::GPU::GPU_vendor_Intel)
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D INTEL_ON");
    if(ints==model_start_gid)
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D GID_UPD");
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D SUN=%u",         lattice_group);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ND=%u",          lattice_nd);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N1=%u",          lattice_domain_n1);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N2=%u",          lattice_domain_size[1]);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N3=%u",          lattice_domain_size[2]);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D N4=%u",          lattice_domain_size[3]);
    if (!((PHI==0.0)&&(OMEGA==0.0))) options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D TBC");     // turn on TBC
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s",           GPU0->cl_root_path,path_suncl);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -I %s%s",           GPU0->cl_root_path,path_kernel);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D ROWSIZE=%u",     lattice_table_row_size);
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRECISION=%u",   precision);
    
    options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PLK=%u",   getK(lattice_domain_n1, lattice_domain_size[1], GPU0->GPU_limit_max_workgroup_size));

    if (PRNG0->PRNG_precision == PRNG_CL::PRNG::PRNG_precision_double)
        options_length_common += sprintf_s(options_common + options_length_common,sizeof(options_common)-options_length_common," -D PRNG_PRECISION=2");
    
    char options[1024];
    int options_length  = sprintf_s(options,sizeof(options),"%s",options_common);
        options_length += sprintf_s(options + options_length,sizeof(options)-options_length," -D NHIT=%u",        NHIT);
        options_length += sprintf_s(options + options_length,sizeof(options)-options_length," -D NHITPar=%u",     NHITPar);
        options_length += sprintf_s(options + options_length,sizeof(options)-options_length," -D PRNGSTEP=%u",    lattice_table_row_size_half);

    char buffer_update_cl[FNAME_MAX_LENGTH];
        j = sprintf_s(buffer_update_cl  ,FNAME_MAX_LENGTH,  "%s",GPU0->cl_root_path);
        j+= sprintf_s(buffer_update_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_UPDATE);
    char* update_source       = GPU0->source_read(buffer_update_cl);
                                GPU0->program_create(update_source,options);


    // SU(3)__________________________________________________________________________________

    const size_t init_global_size[]               = {lattice_table_exact_row_size};
    const size_t init_hot_global_size[]           = {lattice_group_elements[lattice_group-1]*lattice_table_row_size};
    const size_t monte_global_size[]              = {lattice_table_exact_row_size_half};

    const size_t measurement3_global_size[]       = {lattice_action_size};
    const size_t polyakov3_global_size[]          = {lattice_polyakov_size};
    const size_t clear_measurement_global_size[]  = {lattice_measurement_size_F};

    const size_t reduce_measurement_global_size[] = {GPU0->GPU_info.max_workgroup_size};
    const size_t reduce_polyakov_global_size[]    = {GPU0->GPU_info.max_workgroup_size};
    const size_t reduce_local_size[3]             = {GPU0->GPU_info.max_workgroup_size};

    const size_t local_size_lattice_measurement[] = {GPU0->GPU_info.max_workgroup_size};
    const size_t local_size_lattice_polyakov[]    = {GPU0->GPU_info.max_workgroup_size};
    const size_t local_size_lattice_wilson[]      = {GPU0->GPU_info.max_workgroup_size};


    if (ints==model_start_gid) {            // gid init
                sun_init_id = GPU0->kernel_init("lattice_init_gid",1,init_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_id,lattice_table);
    } else if (ints==model_start_cold) {    // cold init
                sun_init_id = GPU0->kernel_init("lattice_init_cold",1,init_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_id,lattice_table);
    } else {                                // hot init
                sun_init_X_id = GPU0->kernel_init("lattice_init_hot_X",1,init_hot_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_X_id,lattice_table);
                argument_id = GPU0->kernel_init_buffer(sun_init_X_id,PRNG0->PRNG_randoms_id);
                sun_init_Y_id = GPU0->kernel_init("lattice_init_hot_Y",1,init_hot_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_Y_id,lattice_table);
                argument_id = GPU0->kernel_init_buffer(sun_init_Y_id,PRNG0->PRNG_randoms_id);
                sun_init_Z_id = GPU0->kernel_init("lattice_init_hot_Z",1,init_hot_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_Z_id,lattice_table);
                argument_id = GPU0->kernel_init_buffer(sun_init_Z_id,PRNG0->PRNG_randoms_id);
                sun_init_T_id = GPU0->kernel_init("lattice_init_hot_T",1,init_hot_global_size,NULL);
                argument_id = GPU0->kernel_init_buffer(sun_init_T_id,lattice_table);
                argument_id = GPU0->kernel_init_buffer(sun_init_T_id,PRNG0->PRNG_randoms_id);
    }

    sun_GramSchmidt_id = GPU0->kernel_init("lattice_GramSchmidt",1,init_global_size,NULL); 
           argument_id = GPU0->kernel_init_buffer(sun_GramSchmidt_id,lattice_table);
           argument_id = GPU0->kernel_init_buffer(sun_GramSchmidt_id,lattice_parameters);

    sun_update_odd_X_id = GPU0->kernel_init("update_odd_X",1,monte_global_size,NULL);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_X_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_X_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_X_id,PRNG0->PRNG_randoms_id);

    sun_update_even_X_id = GPU0->kernel_init("update_even_X",1,monte_global_size,NULL);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_X_id,lattice_table);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_X_id,lattice_parameters);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_X_id,PRNG0->PRNG_randoms_id);

    sun_update_odd_Y_id = GPU0->kernel_init("update_odd_Y",1,monte_global_size,NULL);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Y_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Y_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Y_id,PRNG0->PRNG_randoms_id);

    sun_update_even_Y_id = GPU0->kernel_init("update_even_Y",1,monte_global_size,NULL);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Y_id,lattice_table);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Y_id,lattice_parameters);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Y_id,PRNG0->PRNG_randoms_id);

    sun_update_odd_Z_id = GPU0->kernel_init("update_odd_Z",1,monte_global_size,NULL);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Z_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Z_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_Z_id,PRNG0->PRNG_randoms_id);

    sun_update_even_Z_id = GPU0->kernel_init("update_even_Z",1,monte_global_size,NULL);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Z_id,lattice_table);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Z_id,lattice_parameters);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_Z_id,PRNG0->PRNG_randoms_id);

    sun_update_odd_T_id = GPU0->kernel_init("update_odd_T",1,monte_global_size,NULL);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_T_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_T_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_update_odd_T_id,PRNG0->PRNG_randoms_id);

    sun_update_even_T_id = GPU0->kernel_init("update_even_T",1,monte_global_size,NULL);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_T_id,lattice_table);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_T_id,lattice_parameters);
             argument_id = GPU0->kernel_init_buffer(sun_update_even_T_id,PRNG0->PRNG_randoms_id);

    // for all measurements _____________________________________________________________________________________________________________________________________
    char options_measurements[1024];
    int options_measurement_length  = sprintf_s(options_measurements,sizeof(options_measurements),"%s",options_common);

    if (get_Fmunu) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU");   // calculate tensor Fmunu for H field
    if (get_F0mu)  options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D F0MU");   // calculate tensor Fmunu for E field

    if ((get_Fmunu)||(get_F0mu)) {
        if (get_Fmunu1) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU1");   // calculate tensor Fmunu for lambda1 matrix
        if (get_Fmunu2) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU2");   // calculate tensor Fmunu for lambda2 matrix
        if (get_Fmunu4) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU4");   // calculate tensor Fmunu for lambda4 matrix
        if (get_Fmunu5) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU5");   // calculate tensor Fmunu for lambda5 matrix
        if (get_Fmunu6) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU6");   // calculate tensor Fmunu for lambda6 matrix
        if (get_Fmunu7) options_measurement_length += sprintf_s(options_measurements + options_measurement_length,sizeof(options_measurements)-options_measurement_length," -D FMUNU7");   // calculate tensor Fmunu for lambda7 matrix
    }
    char buffer_measurements_cl[FNAME_MAX_LENGTH];
        j = sprintf_s(buffer_measurements_cl  ,FNAME_MAX_LENGTH,  "%s",GPU0->cl_root_path);
        j+= sprintf_s(buffer_measurements_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_MEASUREMENTS);
    char* measurements_source       = GPU0->source_read(buffer_measurements_cl);
                                GPU0->program_create(measurements_source,options_measurements);

    int size_reduce_measurement_plq_double2   = 0;
    int offset_reduce_measurement_plq_double2 = 0;

    sun_measurement_plq_id        = 0;
    sun_measurement_reduce_id     = 0;
    sun_measurement_plq_reduce_id = 0;

    sun_clear_measurement_id = GPU0->kernel_init("clear_measurement",1,clear_measurement_global_size,NULL);
                 argument_id = GPU0->kernel_init_buffer(sun_clear_measurement_id,lattice_measurement);

    sun_measurement_id = GPU0->kernel_init("lattice_measurement",1,measurement3_global_size,local_size_lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_measurement_id,lattice_table);
           argument_id = GPU0->kernel_init_buffer(sun_measurement_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_measurement_id,lattice_parameters);
           argument_id = GPU0->kernel_init_buffer(sun_measurement_id,lattice_lds);
    int size_reduce_measurement_double2 = (int) ceil((double) lattice_table_exact_row_size / GPU0->kernel_get_worksize(sun_measurement_id));

    printf("GPU0->kernel_get_worksize(sun_measurement_id) = %i\n", GPU0->kernel_get_worksize(sun_measurement_id));
    
    sun_measurement_reduce_id = GPU0->kernel_init("reduce_measurement_double2",1,reduce_measurement_global_size,reduce_local_size);
                  argument_id = GPU0->kernel_init_buffer(sun_measurement_reduce_id,lattice_measurement);
                  argument_id = GPU0->kernel_init_buffer(sun_measurement_reduce_id,lattice_energies);
                  argument_id = GPU0->kernel_init_buffer(sun_measurement_reduce_id,lattice_lds);
                  argument_measurement_index = GPU0->kernel_init_constant(sun_measurement_reduce_id,&size_reduce_measurement_double2);
          
    if(get_actions_diff)
    {
      cl_uint4 Sparam;
    Sparam.s[0] = (int) ceil((double) lattice_action_size / GPU0->kernel_get_worksize(sun_measurement_id));
    Sparam.s[1] = lattice_energies_size;
    Sparam.s[2] = 0;
    Sparam.s[3] = 0;
      
    sun_action_diff_x_id = GPU0->kernel_init("lattice_action_diff_x",1,measurement3_global_size,local_size_lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_id,lattice_table);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_id,lattice_parameters);	
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_id,lattice_lds);
        int size_reduce_action_diff_double2 = (int) ceil((double) lattice_action_size / GPU0->kernel_get_worksize(sun_action_diff_x_id));
    
        sun_action_diff_x_reduce_id = GPU0->kernel_init("reduce_action_diff_x_double2",1,reduce_measurement_global_size,reduce_local_size);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_reduce_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_reduce_id,lattice_action_diff_x);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_x_reduce_id,lattice_lds);
           argument_action_diff_x_index = GPU0->kernel_init_constant(sun_action_diff_x_reduce_id,&Sparam);
       
    sun_action_diff_y_id = GPU0->kernel_init("lattice_action_diff_y",1,measurement3_global_size,local_size_lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_id,lattice_table);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_id,lattice_parameters);	
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_id,lattice_lds);
        size_reduce_action_diff_double2 = (int) ceil((double) lattice_action_size / GPU0->kernel_get_worksize(sun_action_diff_y_id));
    
        sun_action_diff_y_reduce_id = GPU0->kernel_init("reduce_action_diff_y_double2",1,reduce_measurement_global_size,reduce_local_size);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_reduce_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_reduce_id,lattice_action_diff_y);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_y_reduce_id,lattice_lds);
           argument_action_diff_y_index = GPU0->kernel_init_constant(sun_action_diff_y_reduce_id,&Sparam);
       
    sun_action_diff_z_id = GPU0->kernel_init("lattice_action_diff_z",1,measurement3_global_size,local_size_lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_id,lattice_table);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_id,lattice_parameters);	
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_id,lattice_lds);
        size_reduce_action_diff_double2 = (int) ceil((double) lattice_action_size / GPU0->kernel_get_worksize(sun_action_diff_z_id));
    
        sun_action_diff_z_reduce_id = GPU0->kernel_init("reduce_action_diff_z_double2",1,reduce_measurement_global_size,reduce_local_size);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_reduce_id,lattice_measurement);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_reduce_id,lattice_action_diff_z);
           argument_id = GPU0->kernel_init_buffer(sun_action_diff_z_reduce_id,lattice_lds);
           argument_action_diff_z_index = GPU0->kernel_init_constant(sun_action_diff_z_reduce_id,&Sparam);
    }

    // for mean averaged plaquettes measurements
    cl_uint4 mesurement_plq_param;
    if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) {
        sun_measurement_plq_id = GPU0->kernel_init("lattice_measurement_plq",1,measurement3_global_size,local_size_lattice_measurement);
                   offset_reduce_measurement_plq_double2 = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_table_exact_row_size / GPU0->kernel_get_worksize(sun_measurement_plq_id)),GPU0->kernel_get_worksize(sun_measurement_plq_id));
                   argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_id,lattice_table);
                   argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_id,lattice_measurement);
                   argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_id,lattice_parameters);
                   argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_id,lattice_lds);
                   argument_id = GPU0->kernel_init_constant(sun_measurement_plq_id,&offset_reduce_measurement_plq_double2);
        size_reduce_measurement_plq_double2 = (int) ceil((double) lattice_table_exact_row_size / GPU0->kernel_get_worksize(sun_measurement_plq_id));

        sun_measurement_plq_reduce_id = GPU0->kernel_init("reduce_measurement_plq_double2",1,reduce_measurement_global_size,reduce_local_size);
                          argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_reduce_id,lattice_measurement);
                          argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_reduce_id,lattice_energies_plq);
                          argument_id = GPU0->kernel_init_buffer(sun_measurement_plq_reduce_id,lattice_lds);
            mesurement_plq_param.s[0] = size_reduce_measurement_plq_double2;
            mesurement_plq_param.s[1] = offset_reduce_measurement_plq_double2;
            mesurement_plq_param.s[2] = lattice_energies_offset;
            mesurement_plq_param.s[3] = 0;
                          argument_plq_index = GPU0->kernel_init_constant(sun_measurement_plq_reduce_id,&mesurement_plq_param);
    }
    
    int lattice_measurement_size_correction = max(size_reduce_measurement_double2,size_reduce_measurement_plq_double2);
    if (lattice_measurement_size_F<(unsigned int) lattice_measurement_size_correction){
        printf ("buffer plattice_measurement should be resized!!!\n");
        _getch();
    }

    // for Wilson loop measurements _____________________________________________________________________________________________________________________________
    char options_wilson[1024];
    sprintf_s(options_wilson,sizeof(options_wilson),"%s",options_common);

    char buffer_wilson_cl[FNAME_MAX_LENGTH];
        j = sprintf_s(buffer_wilson_cl  ,FNAME_MAX_LENGTH,  "%s",GPU0->cl_root_path);
        j+= sprintf_s(buffer_wilson_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_WILSON_LOOP);
    char* wilson_source       = GPU0->source_read(buffer_wilson_cl);
                                GPU0->program_create(wilson_source,options_wilson);

    int size_reduce_wilson_double2 = 0;

    sun_measurement_wilson_id  = 0;
    sun_wilson_loop_reduce_id  = 0;
    if (get_wilson_loop) {
        sun_measurement_wilson_id = GPU0->kernel_init("lattice_measurement_wilson",1,measurement3_global_size,local_size_lattice_wilson);
                      argument_id = GPU0->kernel_init_buffer(sun_measurement_wilson_id,lattice_table);
                      argument_id = GPU0->kernel_init_buffer(sun_measurement_wilson_id,lattice_measurement);
                      argument_id = GPU0->kernel_init_buffer(sun_measurement_wilson_id,lattice_parameters);
                      argument_id = GPU0->kernel_init_buffer(sun_measurement_wilson_id,lattice_lds);
        size_reduce_wilson_double2 = (int) ceil((double) lattice_domain_exact_site / GPU0->kernel_get_worksize(sun_measurement_wilson_id));

        sun_wilson_loop_reduce_id = GPU0->kernel_init("reduce_wilson_double2",1,reduce_measurement_global_size,reduce_local_size);
                      argument_id = GPU0->kernel_init_buffer(sun_wilson_loop_reduce_id,lattice_measurement);
                      argument_id = GPU0->kernel_init_buffer(sun_wilson_loop_reduce_id,lattice_wilson_loop);
                      argument_id = GPU0->kernel_init_buffer(sun_wilson_loop_reduce_id,lattice_lds);
                      argument_wilson_index = GPU0->kernel_init_constant(sun_wilson_loop_reduce_id,&size_reduce_wilson_double2);
                      // setup index for wilson loop is before kernel run
    }

    // for Polyakov loop measurements ___________________________________________________________________________________________________________________________
    char options_polyakov[1024];
    int options_length_polyakov  = sprintf_s(options_polyakov,sizeof(options_polyakov),"%s",options_common);
        options_length_polyakov += sprintf_s(options_polyakov + options_length_polyakov,sizeof(options_polyakov)-options_length_polyakov," -D PL=%u",          PL_level);

    char buffer_polyakov_cl[FNAME_MAX_LENGTH];
        j = sprintf_s(buffer_polyakov_cl  ,FNAME_MAX_LENGTH,  "%s",GPU0->cl_root_path);
        j+= sprintf_s(buffer_polyakov_cl+j,FNAME_MAX_LENGTH-j,"%s",SOURCE_POLYAKOV);
    char* polyakov_source       = GPU0->source_read(buffer_polyakov_cl);
                                  GPU0->program_create(polyakov_source,options_polyakov);
    int size_reduce_polyakov_double2   = 0;
    int offset_reduce_polyakov_double2 = 0;
    int size_reduce_polyakov_diff_double2   = 0;
    int offset_reduce_polyakov_diff_double2 = 0;
    sun_polyakov_id        = 0;
    sun_polyakov_reduce_id = 0;
    sun_polyakov_diff_x_id        = 0;
    sun_polyakov_diff_x_reduce_id = 0;
    sun_polyakov_diff_y_id        = 0;
    sun_polyakov_diff_y_reduce_id = 0;
    sun_polyakov_diff_z_id        = 0;
    sun_polyakov_diff_z_reduce_id = 0;
    cl_uint4 polyakov_param;
    if (PL_level > 0) {
        sun_polyakov_id = GPU0->kernel_init("lattice_polyakov",1,polyakov3_global_size,local_size_lattice_polyakov);
        offset_reduce_polyakov_double2 = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_id)),GPU0->kernel_get_worksize(sun_polyakov_id));
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_id,lattice_measurement);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_id,lattice_lds);
            argument_id = GPU0->kernel_init_constant(sun_polyakov_id,&offset_reduce_polyakov_double2);
        size_reduce_polyakov_double2 = (int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_id));

        sun_polyakov_reduce_id = GPU0->kernel_init("reduce_polyakov_double2",1,reduce_polyakov_global_size,reduce_local_size);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_reduce_id,lattice_measurement);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_reduce_id,lattice_polyakov_loop);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_reduce_id,lattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_double2;
            polyakov_param.s[2] = lattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            argument_polyakov_index = GPU0->kernel_init_constant(sun_polyakov_reduce_id,&polyakov_param);
    }
    if (PL_level > 2) {
        sun_polyakov_diff_x_id = GPU0->kernel_init("lattice_polyakov_diff_x",1,polyakov3_global_size,local_size_lattice_polyakov);
        offset_reduce_polyakov_diff_double2 = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_x_id)),GPU0->kernel_get_worksize(sun_polyakov_diff_x_id));
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_id,lattice_measurement);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_id,lattice_lds);
            argument_id = GPU0->kernel_init_constant(sun_polyakov_diff_x_id,&offset_reduce_polyakov_diff_double2);
        size_reduce_polyakov_diff_double2 = (int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_x_id));

        sun_polyakov_diff_x_reduce_id = GPU0->kernel_init("reduce_polyakov_diff_x_double2",1,reduce_polyakov_global_size,reduce_local_size); 
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_reduce_id,lattice_measurement);
        argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_reduce_id,lattice_polyakov_loop_diff_x);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_x_reduce_id,lattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = lattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            argument_polyakov_diff_x_index = GPU0->kernel_init_constant(sun_polyakov_diff_x_reduce_id,&polyakov_param);
            
        sun_polyakov_diff_y_id = GPU0->kernel_init("lattice_polyakov_diff_y",1,polyakov3_global_size,local_size_lattice_polyakov);
        offset_reduce_polyakov_diff_double2 = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_y_id)),GPU0->kernel_get_worksize(sun_polyakov_diff_y_id));
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_id,lattice_measurement);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_id,lattice_lds);
            argument_id = GPU0->kernel_init_constant(sun_polyakov_diff_y_id,&offset_reduce_polyakov_diff_double2);
        size_reduce_polyakov_diff_double2 = (int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_y_id));

        sun_polyakov_diff_y_reduce_id = GPU0->kernel_init("reduce_polyakov_diff_y_double2",1,reduce_polyakov_global_size,reduce_local_size); 
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_reduce_id,lattice_measurement);
        argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_reduce_id,lattice_polyakov_loop_diff_y);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_y_reduce_id,lattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = lattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            argument_polyakov_diff_y_index = GPU0->kernel_init_constant(sun_polyakov_diff_y_reduce_id,&polyakov_param);
        
    sun_polyakov_diff_z_id = GPU0->kernel_init("lattice_polyakov_diff_z",1,polyakov3_global_size,local_size_lattice_polyakov);
        offset_reduce_polyakov_diff_double2 = GPU0->buffer_size_align((unsigned int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_z_id)),GPU0->kernel_get_worksize(sun_polyakov_diff_z_id));
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_id,lattice_table);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_id,lattice_measurement);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_id,lattice_parameters);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_id,lattice_lds);
            argument_id = GPU0->kernel_init_constant(sun_polyakov_diff_z_id,&offset_reduce_polyakov_diff_double2);
        size_reduce_polyakov_diff_double2 = (int) ceil((double) lattice_polyakov_size / GPU0->kernel_get_worksize(sun_polyakov_diff_z_id));

        sun_polyakov_diff_z_reduce_id = GPU0->kernel_init("reduce_polyakov_diff_z_double2",1,reduce_polyakov_global_size,reduce_local_size); 
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_reduce_id,lattice_measurement);
        argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_reduce_id,lattice_polyakov_loop_diff_z);
            argument_id = GPU0->kernel_init_buffer(sun_polyakov_diff_z_reduce_id,lattice_lds);
            polyakov_param.s[0] = size_reduce_polyakov_diff_double2;
            polyakov_param.s[1] = offset_reduce_polyakov_diff_double2;
            polyakov_param.s[2] = lattice_polyakov_loop_size;
            polyakov_param.s[3] = 0;
            argument_polyakov_diff_z_index = GPU0->kernel_init_constant(sun_polyakov_diff_z_reduce_id,&polyakov_param);
    }
}
#endif

#ifdef BIGLAT
void        model::lattice_create_buffers(void)
{
    int lds_size;

    int fc = 1;
    int fc2 = 1;

    int a, kk, x, dNx;
#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(int k = 0; k < lattice_Nparts; k++){
#endif
        SubLat[k].size_sublattice_table = fc * SubLat[k].sublattice_table_Size;
        SubLat[k].size_sublattice_table_small = fc * SubLat[k].sublattice_table_size;
        SubLat[k].size_sublattice_measurement   = fc * SubLat[k].sublattice_measurement_size;
        if(PL_level >= 2)
            SubLat[k].size_sublattice_measurement = 2 * fc * SubLat[k].sublattice_measurement_size;
        if(get_Fmunu || get_F0mu)
            SubLat[k].size_sublattice_measurement = MODEL_energies_size * fc * SubLat[k].sublattice_measurement_size;
        if(PL_level == 3 || get_actions_diff){
            fc2 = SubLat[k].Nx;
            if(SubLat[k].Ny > fc2) fc2 = SubLat[k].Ny;
            if(SubLat[k].Nz > fc2) fc2 = SubLat[k].Nz;

            if(fc2 == SubLat[k].Nx){
                SubLat[k].size_sublattice_measurement_diff = SubLat[k].Nx * SubLat[k].GPU0->buffer_size_align((unsigned int) (ceil((double)(getK(SubLat[k].Ny, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Ny * SubLat[k].Nt) / SubLat[k].GPU0->GPU_info.max_workgroup_size)));
            }
            if(fc2 == SubLat[k].Ny){
                SubLat[k].size_sublattice_measurement_diff = SubLat[k].Ny * SubLat[k].GPU0->buffer_size_align((unsigned int) (ceil((double)(getK(SubLat[k].Nx, SubLat[k].Nz, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Nt) / SubLat[k].GPU0->GPU_info.max_workgroup_size)));
            }
            if(fc2 == SubLat[k].Nz){
                SubLat[k].size_sublattice_measurement_diff = SubLat[k].Nz * SubLat[k].GPU0->buffer_size_align((unsigned int) (ceil((double)(getK(SubLat[k].Nx, SubLat[k].Ny, SubLat[k].GPU0->GPU_limit_max_workgroup_size) * SubLat[k].Nx * SubLat[k].Nt) / SubLat[k].GPU0->GPU_info.max_workgroup_size)));
            }
        }
        SubLat[k].size_sublattice_parameters    = fc * SubLat[k].sublattice_parameters_size;
        SubLat[k].size_sublattice_plq  = fc * SubLat[k].iterr;
        if(get_Fmunu || get_F0mu)
            SubLat[k].size_sublattice_plq  = fc * SubLat[k].iterr * MODEL_energies_size;
        SubLat[k].size_sublattice_energies      = fc * SubLat[k].iterr;

        SubLat[k].size_sublattice_polyakov_loop = fc * SubLat[k].iterr * ((PL_level > 2) ? 2 : PL_level);
        if(get_wilson_loop){
            SubLat[k].size_sublattice_Lt  = fc * SubLat[k].sublattice_Lt_size;
            SubLat[k].size_sublattice_Lr1 = fc * SubLat[k].sublattice_Lr1_size;
            SubLat[k].size_sublattice_Lr2 = fc * SubLat[k].sublattice_Lr2_size;
            SubLat[k].size_sublattice_WLx  = fc * SubLat[k].sublattice_WLx_size;
            SubLat[k].size_sublattice_wlx  = fc * SubLat[k].sublattice_wlx_size;

            SubLat[k].m = (int*)calloc(SubLat[k].Nx, sizeof(int));
            SubLat[k].dx = (int*)calloc(SubLat[k].Nx, sizeof(int));

            for(x = 0; x < SubLat[k].Nx; x++){
                a = x + wilson_R;
                dNx = SubLat[k].Nx;
                kk = k;
                while(dNx < a){
                    kk++;
                    dNx += SubLat[kk % lattice_Nparts].Nx;
                }
                SubLat[k].m[x] = kk;
                SubLat[k].dx[x] = SubLat[SubLat[k].m[x]].Nx - (dNx - a);
            }
            printf("\n");
        }
    }
    
#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(int k = 0; k < lattice_Nparts; k++){
#endif
        
        if (precision == model_precision_single) {
            SubLat[k].psublattice_table_float = (cl_float4*) calloc(SubLat[k].size_sublattice_table, sizeof(cl_float4));
#ifdef BIGTOSMALL
            SubLat[k].psublattice_table_small_float = (cl_float4*)calloc(SubLat[k].size_sublattice_table_small, sizeof(cl_float4));
#endif
        } else {
            SubLat[k].psublattice_table_double = (cl_double4*) calloc(SubLat[k].size_sublattice_table, sizeof(cl_double4));
#ifdef BIGTOSMALL
            SubLat[k].psublattice_table_small_double = (cl_double4*)calloc(SubLat[k].size_sublattice_table_small, sizeof(cl_double4));
#endif
        }

        //if (INIT==0) lattice_load_state();  // load state file if needed

        lds_size = (int) SubLat[k].GPU0->GPU_info.local_memory_size / 4 / 4 / 2;  // 4 bytes per component, 4 components, double2 type <- use all local memory

        if(precision == model_precision_single)
        {
            SubLat[k].psublattice_parameters_float       = (cl_float*)   calloc(SubLat[k].size_sublattice_parameters,sizeof(cl_float));
            SubLat[k].psublattice_parameters_float[0]    = (float) (BETA / lattice_group);
            SubLat[k].psublattice_parameters_float[1]    = (float) (PHI);
            SubLat[k].psublattice_parameters_float[2]    = (float) (OMEGA);
            SubLat[k].psublattice_parameters_float[3]    = (float) (wilson_R);
            SubLat[k].psublattice_parameters_float[4]    = (float) (wilson_T);
        } else {
            SubLat[k].psublattice_parameters_double      = (cl_double*)  calloc(SubLat[k].size_sublattice_parameters,sizeof(cl_double));
            SubLat[k].psublattice_parameters_double[0]   = (double) (BETA / lattice_group);
            SubLat[k].psublattice_parameters_double[1]   = (double) (PHI);
            SubLat[k].psublattice_parameters_double[2]   = (double) (OMEGA);
            SubLat[k].psublattice_parameters_double[3]   = (double) (wilson_R);
            SubLat[k].psublattice_parameters_double[4]   = (double) (wilson_T);
        }

        SubLat[k].sublattice_table = 0;
        if (precision == model_precision_single) {
            //SINGLE PREC.-----------------------------------------
            SubLat[k].sublattice_table = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_table, SubLat[k].psublattice_table_float, sizeof(cl_float4));  // Lattice data

            SubLat[k].sublattice_parameters      = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_Constant, SubLat[k].size_sublattice_parameters, SubLat[k].psublattice_parameters_float,  sizeof(cl_float));   // Lattice counters and indices
            //-----------------------------------------------------
        } else {
            //DOUBLE PREC.-----------------------------------------
            SubLat[k].sublattice_table = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_table, SubLat[k].psublattice_table_double, sizeof(cl_double4));  // Lattice data

            SubLat[k].sublattice_parameters      = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_parameters, SubLat[k].psublattice_parameters_double, sizeof(cl_double));  // Lattice counters and indices
            //-----------------------------------------------------
        }
        
        SubLat[k].sublattice_lds = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_LDS, lds_size, NULL, sizeof(cl_double2)); // LDS for reduction
        
        SubLat[k].psublattice_measurement    = (cl_double2*) calloc(SubLat[k].size_sublattice_measurement,  sizeof(cl_double2));
        SubLat[k].sublattice_measurement         = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_measurement,      SubLat[k].psublattice_measurement,       sizeof(cl_double2)); // Lattice measurement
        if(PL_level == 3 || get_actions_diff){
            SubLat[k].psublattice_measurement_diff = (cl_double2*) calloc(SubLat[k].size_sublattice_measurement_diff,  sizeof(cl_double2));
            SubLat[k].sublattice_measurement_diff  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_measurement_diff, SubLat[k].psublattice_measurement_diff, sizeof(cl_double2)); // Lattice measurement
        }

        SubLat[k].psublattice_plq   = (cl_double2*) calloc(SubLat[k].size_sublattice_plq, sizeof(cl_double2));
        if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu))
            SubLat[k].sublattice_plq    = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_plq, SubLat[k].psublattice_plq, sizeof(cl_double2)); // Lattice energies (plaquettes)

        SubLat[k].psublattice_energies = (cl_double2*) calloc(SubLat[k].size_sublattice_energies, sizeof(cl_double2));
        SubLat[k].sublattice_energies  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_energies, SubLat[k].psublattice_energies, sizeof(cl_double2)); // Lattice energies

        SubLat[k].psublattice_action_diff_x      = NULL;
        SubLat[k].psublattice_action_diff_y      = NULL;
        SubLat[k].psublattice_action_diff_z      = NULL;
        if(get_actions_diff)
        {
            SubLat[k].psublattice_action_diff_x      = (cl_double2*) calloc(SubLat[k].Nx * SubLat[k].size_sublattice_energies,     sizeof(cl_double2)); 
            SubLat[k].psublattice_action_diff_y      = (cl_double2*) calloc(SubLat[k].Ny * SubLat[k].size_sublattice_energies,     sizeof(cl_double2)); 
            SubLat[k].psublattice_action_diff_z      = (cl_double2*) calloc(SubLat[k].Nz * SubLat[k].size_sublattice_energies,     sizeof(cl_double2)); 

            SubLat[k].sublattice_action_diff_x      = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Nx*SubLat[k].size_sublattice_energies, SubLat[k].psublattice_action_diff_x, sizeof(cl_double2));
            SubLat[k].sublattice_action_diff_y      = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Ny*SubLat[k].size_sublattice_energies, SubLat[k].psublattice_action_diff_y, sizeof(cl_double2));
            SubLat[k].sublattice_action_diff_z      = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Nz*SubLat[k].size_sublattice_energies, SubLat[k].psublattice_action_diff_z, sizeof(cl_double2));
        }

        SubLat[k].psublattice_polyakov_loop  = NULL;
        if (PL_level > 0) 
            SubLat[k].psublattice_polyakov_loop = (cl_double2*) calloc(SubLat[k].size_sublattice_polyakov_loop,sizeof(cl_double2));
        SubLat[k].psublattice_polyakov_loop_diff_x  = NULL;
        SubLat[k].psublattice_polyakov_loop_diff_y  = NULL;
        SubLat[k].psublattice_polyakov_loop_diff_z  = NULL;
        if (PL_level > 2) 
        {
            SubLat[k].psublattice_polyakov_loop_diff_x = (cl_double2*) calloc(SubLat[k].Nx*SubLat[k].sublattice_polyakov_loop_size,sizeof(cl_double2));
            SubLat[k].psublattice_polyakov_loop_diff_y = (cl_double2*) calloc(SubLat[k].Ny*SubLat[k].sublattice_polyakov_loop_size,sizeof(cl_double2));
            SubLat[k].psublattice_polyakov_loop_diff_z = (cl_double2*) calloc(SubLat[k].Nz*SubLat[k].sublattice_polyakov_loop_size,sizeof(cl_double2));
        }
        
        if (PL_level > 0)
            SubLat[k].sublattice_polyakov_loop   = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_polyakov_loop, SubLat[k].psublattice_polyakov_loop, sizeof(cl_double2)); // Polyakov loops
        if (PL_level > 2)
        {
            SubLat[k].sublattice_polyakov_loop_diff_x   = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Nx*SubLat[k].sublattice_polyakov_loop_size,    SubLat[k].psublattice_polyakov_loop_diff_x, sizeof(cl_double2));
            SubLat[k].sublattice_polyakov_loop_diff_y   = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Ny*SubLat[k].sublattice_polyakov_loop_size,    SubLat[k].psublattice_polyakov_loop_diff_y, sizeof(cl_double2));
            SubLat[k].sublattice_polyakov_loop_diff_z   = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Nz*SubLat[k].sublattice_polyakov_loop_size,    SubLat[k].psublattice_polyakov_loop_diff_z, sizeof(cl_double2));
        }

        if(get_wilson_loop){
            SubLat[k].psublattice_Lt = (cl_double4*) calloc(SubLat[k].size_sublattice_Lt, sizeof(cl_double4));
            SubLat[k].sublattice_Lt = 0;
            SubLat[k].sublattice_Lt  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_Lt, SubLat[k].psublattice_Lt, sizeof(cl_double4));

            SubLat[k].psublattice_Lr1 = (cl_double4*) calloc(SubLat[k].size_sublattice_Lr1, sizeof(cl_double4));
            SubLat[k].sublattice_Lr1 = 0;
            SubLat[k].sublattice_Lr1  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_Lr1, SubLat[k].psublattice_Lr1, sizeof(cl_double4));

            if(SubLat[k].size_sublattice_Lr2){
                SubLat[k].psublattice_Lr2 = (cl_double4*) calloc(SubLat[k].size_sublattice_Lr2, sizeof(cl_double4));
                SubLat[k].sublattice_Lr2 = 0;
                SubLat[k].sublattice_Lr2  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_Lr2, SubLat[k].psublattice_Lr2, sizeof(cl_double4));
                
                SubLat[k].size_sublattice_aux = 0;
                int ssize, jj1;
                for(int jj = k; jj <= SubLat[k].m[SubLat[k].Nx - 1]; jj++)
                {
                    jj1 = jj % lattice_Nparts;
                    ssize = (SubLat[jj1].size_sublattice_Lt > SubLat[jj1].size_sublattice_Lr2) ? SubLat[jj1].size_sublattice_Lt : SubLat[jj1].size_sublattice_Lr2;
                    if(SubLat[k].size_sublattice_aux < ssize)
                        SubLat[k].size_sublattice_aux = ssize;
                }

                SubLat[k].psublattice_aux = (cl_double4*) calloc(SubLat[k].size_sublattice_aux, sizeof(cl_double4));
                SubLat[k].sublattice_aux = 0;

                SubLat[k].sublattice_aux  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_aux, SubLat[k].psublattice_aux, sizeof(cl_double4));
            }

            SubLat[k].psublattice_WL_x = (cl_double4*) calloc(SubLat[k].size_sublattice_WLx, sizeof(cl_double4));
            SubLat[k].sublattice_WL_x = 0;
            SubLat[k].sublattice_WL_x  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_WLx, SubLat[k].psublattice_WL_x, sizeof(cl_double4));

            SubLat[k].sublattice_m = 0;
            SubLat[k].sublattice_m = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].Nx, SubLat[k].m, sizeof(int));

            SubLat[k].psublattice_wlx = (cl_double*) calloc(SubLat[k].size_sublattice_wlx, sizeof(cl_double));
            SubLat[k].sublattice_wlx = 0;
            SubLat[k].sublattice_wlx  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_wlx, SubLat[k].psublattice_wlx, sizeof(cl_double));

            SubLat[k].psublattice_wilson_loop = (cl_double*) calloc(SubLat[k].size_sublattice_energies, sizeof(cl_double));
            SubLat[k].sublattice_wilson_loop = 0;
            SubLat[k].sublattice_wilson_loop  = SubLat[k].GPU0->buffer_init(SubLat[k].GPU0->buffer_type_IO, SubLat[k].size_sublattice_energies, SubLat[k].psublattice_wilson_loop, sizeof(cl_double));
        }
    }
}
#else
void        model::lattice_create_buffers(void)
{
    cl_float*    plattice_parameters_float  = NULL;
    cl_double*   plattice_parameters_double = NULL;

    int fc = 1;//2;
int fc2 = 1;
    if ((PL_level>1)&&(lattice_measurement_size_F <= 2 * lattice_polyakov_loop_size)) fc2 = 2;
    if ((PL_level>2)&&(lattice_measurement_size_F <= (lattice_polyakov_loop_size * (lattice_domain_n1 + 1)))) fc2 = lattice_domain_n1 + 1;
    size_lattice_table         = fc * lattice_table_size;
    size_lattice_measurement   = fc * lattice_measurement_size_F;
    size_lattice_energies      = fc * lattice_energies_size;
    size_lattice_wilson_loop   = fc * lattice_energies_size;
    size_lattice_energies_plq  = fc * lattice_energies_offset * MODEL_energies_size;
    
    if(get_actions_diff)
    {
       fc2 = 2; 
       size_lattice_energies *= (fc2*getK(lattice_domain_n1, lattice_domain_size[1], GPU0->GPU_limit_max_workgroup_size));
    }
    
    if (PL_level>2)
      size_lattice_polyakov_loop = fc * fc2 * lattice_polyakov_loop_size * getK(lattice_domain_n1, lattice_domain_size[1], GPU0->GPU_limit_max_workgroup_size);
    else
      size_lattice_polyakov_loop = fc * fc2 * lattice_polyakov_loop_size * PL_level;
    size_lattice_boundary      = fc * lattice_boundary_size;
    size_lattice_parameters    = fc * lattice_parameters_size;

    plattice_table_float    = NULL;
    plattice_table_double   = NULL;
    plattice_boundary_float = NULL;
    plattice_boundary_double= NULL;

    plattice_measurement    = (cl_double2*) calloc(size_lattice_measurement,  sizeof(cl_double2));
    plattice_energies       = (cl_double2*) calloc(size_lattice_energies,     sizeof(cl_double2));
    plattice_energies_plq   = (cl_double2*) calloc(size_lattice_energies_plq, sizeof(cl_double2));
    plattice_wilson_loop    = (cl_double*)  calloc(size_lattice_wilson_loop,  sizeof(cl_double));
    plattice_polyakov_loop  = NULL;
    
    plattice_action_diff_x      = NULL;
    plattice_action_diff_y      = NULL;
    plattice_action_diff_z      = NULL;
    if(get_actions_diff)
    {
       plattice_action_diff_x      = (cl_double2*) calloc(size_lattice_energies,     sizeof(cl_double2)); 
       plattice_action_diff_y      = (cl_double2*) calloc(size_lattice_energies,     sizeof(cl_double2)); 
       plattice_action_diff_z      = (cl_double2*) calloc(size_lattice_energies,     sizeof(cl_double2)); 
    }
        if (PL_level > 0) plattice_polyakov_loop = (cl_double2*) calloc(size_lattice_polyakov_loop,sizeof(cl_double2));
    plattice_polyakov_loop_diff_x  = NULL;
    plattice_polyakov_loop_diff_y  = NULL;
    plattice_polyakov_loop_diff_z  = NULL;
        if (PL_level > 2) 
    {
      plattice_polyakov_loop_diff_x = (cl_double2*) calloc(size_lattice_polyakov_loop,sizeof(cl_double2));
      plattice_polyakov_loop_diff_y = (cl_double2*) calloc(size_lattice_polyakov_loop,sizeof(cl_double2));
      plattice_polyakov_loop_diff_z = (cl_double2*) calloc(size_lattice_polyakov_loop,sizeof(cl_double2));
    }

    if (precision == model_precision_single) {
        plattice_table_float            = (cl_float4*)  calloc(size_lattice_table,     sizeof(cl_float4));
        plattice_boundary_float         = (cl_float4*)  calloc(size_lattice_boundary,  sizeof(cl_float4));
        plattice_parameters_float       = (cl_float*)   calloc(size_lattice_parameters,sizeof(cl_float));
        plattice_parameters_float[0]    = (float) (BETA / lattice_group);
        plattice_parameters_float[1]    = (float) (PHI);
        plattice_parameters_float[2]    = (float) (OMEGA);
        plattice_parameters_float[3]    = (float) (wilson_R);
        plattice_parameters_float[4]    = (float) (wilson_T);
    } else {
        plattice_table_double           = (cl_double4*) calloc(size_lattice_table,     sizeof(cl_double4));
        plattice_boundary_double        = (cl_double4*) calloc(size_lattice_boundary,  sizeof(cl_double4));
        plattice_parameters_double      = (cl_double*)  calloc(size_lattice_parameters,sizeof(cl_double));
        plattice_parameters_double[0]   = (double) (BETA / lattice_group);
        plattice_parameters_double[1]   = (double) (PHI);
        plattice_parameters_double[2]   = (double) (OMEGA);
        plattice_parameters_double[3]   = (double) (wilson_R);
        plattice_parameters_double[4]   = (double) (wilson_T);
    }

    if (INIT==0) lattice_load_state();  // load state file if needed

    int lds_size = (int) GPU0->GPU_info.local_memory_size / 4 / 4 / 2;  // 4 bytes per component, 4 components, double2 type <- use all local memory

    lattice_table = 0;
    if (precision == model_precision_single) {
        lattice_table           = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_table,            plattice_table_float,       sizeof(cl_float4));  // Lattice data
        lattice_boundary        = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_boundary,         plattice_boundary_float,    sizeof(cl_float4));  // Lattice boundary
        lattice_parameters      = GPU0->buffer_init(GPU0->buffer_type_Constant, size_lattice_parameters, plattice_parameters_float,  sizeof(cl_float));   // Lattice counters and indices
    } else {
        lattice_table           = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_table,            plattice_table_double,      sizeof(cl_double4)); // Lattice data
        lattice_boundary        = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_boundary,         plattice_boundary_double,   sizeof(cl_double4)); // Lattice boundary
        lattice_parameters      = GPU0->buffer_init(GPU0->buffer_type_Constant, size_lattice_parameters, plattice_parameters_double, sizeof(cl_double));  // Lattice counters and indices
    }
    lattice_measurement         = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_measurement,      plattice_measurement,       sizeof(cl_double2)); // Lattice measurement
    lattice_lds                 = GPU0->buffer_init(GPU0->buffer_type_LDS,lds_size,                      NULL ,                          sizeof(cl_double2)); // LDS for reduction
    lattice_energies            = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_energies,         plattice_energies,          sizeof(cl_double2)); // Lattice energies
    if(get_actions_diff)
    {
         lattice_action_diff_x      = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_energies,         plattice_action_diff_x,          sizeof(cl_double2));
     lattice_action_diff_y      = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_energies,         plattice_action_diff_y,          sizeof(cl_double2));
     lattice_action_diff_z      = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_energies,         plattice_action_diff_z,          sizeof(cl_double2));
    }
    if ((get_plaquettes_avr) || (get_Fmunu) || (get_F0mu))
        lattice_energies_plq    = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_energies_plq,     plattice_energies_plq,      sizeof(cl_double2)); // Lattice energies (plaquettes)
    if (get_wilson_loop)
        lattice_wilson_loop     = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_wilson_loop,      plattice_wilson_loop,       sizeof(cl_double));  // Wilson loop
    if (PL_level > 0)
        lattice_polyakov_loop   = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_polyakov_loop,    plattice_polyakov_loop, sizeof(cl_double2)); // Polyakov loops
    if (PL_level > 2)
    {
        lattice_polyakov_loop_diff_x   = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_polyakov_loop,    plattice_polyakov_loop_diff_x, sizeof(cl_double2));
        lattice_polyakov_loop_diff_y   = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_polyakov_loop,    plattice_polyakov_loop_diff_y, sizeof(cl_double2));
        lattice_polyakov_loop_diff_z   = GPU0->buffer_init(GPU0->buffer_type_IO, size_lattice_polyakov_loop,    plattice_polyakov_loop_diff_z, sizeof(cl_double2));
    }
}
#endif

#ifdef BIGLAT
#define VER8 //gives incorrect results on AMD GPUs when OpenMP is switched on
//#define VER9 //works on AMD and nVidia GPUs with OpenMP turned on
//#define VER5 //bulk function
#ifdef VER5
void        model::UpdateEdges(int dir)
{
    
}
#endif
#ifdef VER8
void        model::UpdateEdges(int dir)
{
    int k, k_next;

    float *ptr_rf, *ptr_lf;
    double *ptr_rd, *ptr_ld;

    int n1n2n3 = lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3];
    int el_nn = (lattice_group_elements[lattice_group - 1] / 4);// number of 4-vectors

    size_t offset_k, offset_k_next;
    size_t size = 4 * 2 * n1n2n3 * ((precision == model_precision_single) ? sizeof(float) : sizeof(double));

#ifdef USE_OPENMP
    omp_set_num_threads(Ndevices);
#pragma omp parallel private(ptr_rf, ptr_lf, ptr_rd, ptr_ld, k, k_next, offset_k, offset_k_next)
    {
        int f = omp_get_thread_num();
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (k = 0; k < lattice_Nparts; k++){
#endif
        k_next = (k + 1) % lattice_Nparts;
        for (int i = 0; i < el_nn; i++){
            if (precision == model_precision_single){
                offset_k = (SubLat[k].Nx * n1n2n3 + (i * lattice_nd + dir) * SubLat[k].sublattice_table_row_Size) * 4 * sizeof(float);
                offset_k_next = ((i * lattice_nd + dir) * SubLat[k_next].sublattice_table_row_Size) * 4 * sizeof(float);
                ptr_rf = (float*)SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table, offset_k, size);
                ptr_lf = (float*)SubLat[k_next].GPU0->buffer_map(SubLat[k_next].sublattice_table, offset_k_next, size);

                memcpy(ptr_rf + 4 * n1n2n3, ptr_lf + 4 * n1n2n3, 4 * n1n2n3 * sizeof(float));
                memcpy(ptr_lf, ptr_rf, 4 * n1n2n3 * sizeof(float));

                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_table, (void*)ptr_rf);
                SubLat[k_next].GPU0->buffer_unmap(SubLat[k_next].sublattice_table, (void*)ptr_lf);
            }
            else{
                offset_k = (SubLat[k].Nx * n1n2n3 + (i * lattice_nd + dir) * SubLat[k].sublattice_table_row_Size) * 4 * sizeof(double);
                offset_k_next = ((i * lattice_nd + dir) * SubLat[k_next].sublattice_table_row_Size) * 4 * sizeof(double);
                ptr_rd = (double*)SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table, offset_k, size);
#pragma omp barrier
                ptr_ld = (double*)SubLat[k_next].GPU0->buffer_map(SubLat[k_next].sublattice_table, offset_k_next, size);
#pragma omp barrier

                memcpy(ptr_rd + 4 * n1n2n3, ptr_ld + 4 * n1n2n3, 4 * n1n2n3 * sizeof(double));
                memcpy(ptr_ld, ptr_rd, 4 * n1n2n3 * sizeof(double));
#pragma omp barrier
                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_table, (void*)ptr_rd);
#pragma omp barrier
                SubLat[k_next].GPU0->buffer_unmap(SubLat[k_next].sublattice_table, (void*)ptr_ld);
            }
        }
    }
#ifdef USE_OPENMP
                }
#endif
        }
#endif
#ifdef VER9
void        model::UpdateEdges(int dir)
{
    int k, k_next;

    float *ptr_rf, *ptr_lf;
    double *ptr_rd, *ptr_ld;

    int n1n2n3 = lattice_full_size[1] * lattice_full_size[2] * lattice_full_size[3];
    int el_nn = /*lattice_nd * */(lattice_group_elements[lattice_group - 1] / 4);// number of 4-vectors

    size_t offset_k, offset_k_next;
    size_t size = 4 * 2 * n1n2n3 * ((precision == model_precision_single) ? sizeof(float) : sizeof(double));
    
    int tid;

    for (k = 0; k < lattice_Nparts; k++){
        k_next = (k + 1) % lattice_Nparts;
#ifdef USE_OPENMP
        omp_set_num_threads(2);
#pragma omp parallel private(tid)
        { 
            tid = omp_get_thread_num();
#endif
            for (int i = 0; i < el_nn; i++){
                if (precision == model_precision_single){
                    offset_k = (SubLat[k].Nx * n1n2n3 + (i * lattice_nd + dir) * SubLat[k].sublattice_table_row_Size) * 4 * sizeof(float);
                    offset_k_next = ((i * lattice_nd + dir) * SubLat[k_next].sublattice_table_row_Size) * 4 * sizeof(float);
                    if(!tid){
                        ptr_rf = (float*)SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table, offset_k, size);
                    } else {
                        ptr_lf = (float*)SubLat[k_next].GPU0->buffer_map(SubLat[k_next].sublattice_table, offset_k_next, size);
                    }
#pragma omp barrier
                    if(!tid){
                        memcpy(ptr_rf + 4 * n1n2n3, ptr_lf + 4 * n1n2n3, 4 * n1n2n3 * sizeof(float));
                    } else {
                        memcpy(ptr_lf, ptr_rf, 4 * n1n2n3 * sizeof(float));
                    }
                    
                    if(!tid){
                        SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_table, (void*)ptr_rf);
                    } else {
                        SubLat[k_next].GPU0->buffer_unmap(SubLat[k_next].sublattice_table, (void*)ptr_lf);
                    }
                } else {
                    offset_k = (SubLat[k].Nx * n1n2n3 + (i * lattice_nd + dir) * SubLat[k].sublattice_table_row_Size) * 4 * sizeof(double);
                    offset_k_next = ((i * lattice_nd + dir) * SubLat[k_next].sublattice_table_row_Size) * 4 * sizeof(double);
                    if(!tid){
                        ptr_rd = (double*)SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table, offset_k, size);
                    } else {
                        ptr_ld = (double*)SubLat[k_next].GPU0->buffer_map(SubLat[k_next].sublattice_table, offset_k_next, size);
                    }
#pragma omp barrier
                    if(!tid){
                        memcpy(ptr_rd + 4 * n1n2n3, ptr_ld + 4 * n1n2n3, 4 * n1n2n3 * sizeof(double));
                    } else {
                        memcpy(ptr_ld, ptr_rd, 4 * n1n2n3 * sizeof(double));
                    }
                    
                    if(!tid){
                        SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_table, (void*)ptr_rd);
                    } else {
                        SubLat[k_next].GPU0->buffer_unmap(SubLat[k_next].sublattice_table, (void*)ptr_ld);
                    }
                }
            }
#ifdef USE_OPENMP
        }
#endif
    }
}
#endif

void        SubLattice::sublattice_Measure_Plq(unsigned int counter)
{
    GPU0->kernel_run(sun_measurement_plq_id);          // Lattice measurement (plaquettes)
    GPU0->print_stage("measurement done (plaquettes)");
    plq_index = counter;
    GPU0->kernel_init_constant_reset(sun_measurement_plq_reduce_id, &(plq_index), argument_plq_index);
    GPU0->kernel_run(sun_measurement_plq_reduce_id);    // Lattice measurement reduction (plaquettes)
    GPU0->print_stage("measurement reduce done (plaquettes)");
}

void        SubLattice::sublattice_Measure_Action(unsigned int counter)
{
    GPU0->kernel_run(sun_measurement_id);          // Lattice measurement (plaquettes)
    GPU0->print_stage("measurement done");
    measurement_index = counter;
    GPU0->kernel_init_constant_reset(sun_measurement_reduce_id, &(measurement_index), argument_measurement_index);
    GPU0->kernel_run(sun_measurement_reduce_id);    // Lattice measurement reduction (plaquettes)
    GPU0->print_stage("measurement reduce done");
}

void        SubLattice::sublattice_Measure_Action_diff(unsigned int counter){
    GPU0->kernel_run(sun_action_diff_x_id);
    GPU0->print_stage("measurement S diff done");
        action_diff_index = counter;
        GPU0->kernel_init_constant_reset(sun_action_diff_x_reduce_id, &(action_diff_index), argument_action_diff_x_index);
    GPU0->kernel_run(sun_action_diff_x_reduce_id);
    GPU0->print_stage("measurement action diff reduce done");
        
        GPU0->kernel_run(sun_action_diff_y_id);
        GPU0->print_stage("measurement S diff done");
                action_diff_index = counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_y_reduce_id,&action_diff_index,argument_action_diff_y_index);
            GPU0->kernel_run(sun_action_diff_y_reduce_id);
            GPU0->print_stage("measurement action diff reduce done");
        
        GPU0->kernel_run(sun_action_diff_z_id);
        GPU0->print_stage("measurement S diff done");
                action_diff_index = counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_z_reduce_id,&action_diff_index,argument_action_diff_z_index);
            GPU0->kernel_run(sun_action_diff_z_reduce_id);
            GPU0->print_stage("measurement action diff reduce done");
}

void        SubLattice::sublattice_Measure_PL(unsigned int counter)
{
    GPU0->kernel_run(sun_polyakov_id);          // Lattice measurement (plaquettes)
    GPU0->print_stage("measurement done (PL)");
    polyakov_index = counter;
    GPU0->kernel_init_constant_reset(sun_polyakov_reduce_id, &(polyakov_index), argument_polyakov_index);
    GPU0->kernel_run(sun_polyakov_reduce_id);    // Lattice measurement reduction (plaquettes)
    GPU0->print_stage("measurement reduce done (PL)");
}

void        SubLattice::sublattice_Measure_PL_diff(unsigned int counter)
{
    GPU0->kernel_run(sun_polyakov_diff_x_id);                     // Lattice Polyakov loop measurement
    GPU0->print_stage("Polyakov loop diff measurement done");
          polyakov_diff_index = counter;
          GPU0->kernel_init_constant_reset(sun_polyakov_diff_x_reduce_id,&polyakov_diff_index,argument_polyakov_diff_x_index);
    GPU0->kernel_run(sun_polyakov_diff_x_reduce_id);              // Lattice Polyakov loop measurement reduction
    GPU0->print_stage("Polyakov loop diff reduce done");

    GPU0->kernel_run(sun_polyakov_diff_y_id);                     // Lattice Polyakov loop measurement
    GPU0->print_stage("Polyakov loop diff measurement done");
          polyakov_diff_index = counter;
          GPU0->kernel_init_constant_reset(sun_polyakov_diff_y_reduce_id,&polyakov_diff_index,argument_polyakov_diff_y_index);
    GPU0->kernel_run(sun_polyakov_diff_y_reduce_id);              // Lattice Polyakov loop measurement reduction
    GPU0->print_stage("Polyakov loop diff reduce done");
        
    GPU0->kernel_run(sun_polyakov_diff_z_id);                     // Lattice Polyakov loop measurement
    GPU0->print_stage("Polyakov loop diff measurement done");
          polyakov_diff_index = counter;
          GPU0->kernel_init_constant_reset(sun_polyakov_diff_z_reduce_id,&polyakov_diff_index,argument_polyakov_diff_z_index);
    GPU0->kernel_run(sun_polyakov_diff_z_reduce_id);              // Lattice Polyakov loop measurement reduction
    GPU0->print_stage("Polyakov loop diff reduce done");
}

void        SubLattice::sublattice_Measure_WL(unsigned int counter)
{

    GPU0->kernel_run(sun_measurement_Lt_id);
    GPU0->print_stage("measurement Lt done");
    /*printf("measurement Lt done\n");
                    GPU0->buffer_map(sublattice_Lt);
                    GPU0->print_mapped_buffer_double4(sublattice_Lt, 5);
                    //GPU0->print_mapped_buffer_float4(sublattice_Lt, 5);*/
    GPU0->kernel_run(sun_measurement_Lr1_id);
    GPU0->print_stage("measurement Lr1 done");

    if(size_sublattice_Lr2){
        GPU0->kernel_run(sun_measurement_Lr2_id);
        GPU0->print_stage("measurement Lr2 done");
    }
}

#define MAKE_WLX 3
#if MAKE_WLX == 3
void    model::lattice_make_WLx(void){
    int k, M, Nxx;
    int argument_id;
    int n1n2n3;

    int buff_id;
    size_t buff_size;
    int kkk;

    int offs = 0;
    
    void *ptr;
    size_t copy_size;

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++)
#else
    for(k = 0; k < lattice_Nparts; k++)
#endif
        SubLat[k].sublattice_Measure_WL(ITER_counter);

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(k = 0; k < lattice_Nparts; k++){
#endif
        SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_WLx0_id);
        SubLat[k].GPU0->print_stage("measurement WLx0 done");	    
    }

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(k = 0; k < lattice_Nparts; k++){
#endif
        for(int x = 0; x < SubLat[k].Nx; x++)
            if(SubLat[k].m[x] == k){
                SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_WLx1_id);
                SubLat[k].GPU0->print_stage("measurement WLx1 done");
                break;
            }
    }

#ifdef USE_OPENMP
omp_set_num_threads(Ndevices);
#pragma omp parallel private(M, Nxx, kkk, buff_id, buff_size, argument_id, ptr, copy_size)
        { 
                    int f = omp_get_thread_num();
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(k = 0; k < lattice_Nparts; k++){
#endif
        M = SubLat[k].m[0];
        Nxx = SubLat[k].Nx;

        for(int x = 0; x < SubLat[k].Nx; x++)//for x = 1; ...//looking for the part containing right edge of WL for the rightest x
            if(SubLat[k].m[x] > M) M = SubLat[k].m[x]; // �� ������ �� ��������� m?
            //M = SubLat[k].m[SubLat[k].Nx - 1];//why not this?
#pragma omp barrier
        kkk = (k + 1) % lattice_Nparts;
        buff_id = SubLat[kkk].sublattice_Lr2;
        buff_size = SubLat[kkk].GPU0->GPU_buffers[buff_id].size_in_bytes;
        for(int kk = k + 2; kk <= M; kk++){//finding the size of buffer with righter parts of WL
            kkk = kk % lattice_Nparts;
            if(SubLat[kkk].GPU0->GPU_buffers[buff_id].size_in_bytes > buff_size) 
                buff_size = SubLat[kkk].GPU0->GPU_buffers[buff_id].size_in_bytes;
        }
#pragma omp barrier
        for(int kk = k + 1; kk <= M; kk++){
            kkk = kk % lattice_Nparts;
            if(precision == model_precision_single){
                SubLat[k].psublattice_parameters_float = (cl_float*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
                SubLat[k].psublattice_parameters_float[3] = SubLat[kkk].Nx;
                SubLat[k].psublattice_parameters_float[4] = Nxx;
                SubLat[k].psublattice_parameters_float[5] = SubLat[kkk].sublattice_table_yzt_size;
                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, SubLat[k].psublattice_parameters_float);
            } else {
                SubLat[k].psublattice_parameters_double = (cl_double*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
                SubLat[k].psublattice_parameters_double[3] = SubLat[kkk].Nx;
                SubLat[k].psublattice_parameters_double[4] = Nxx;
                SubLat[k].psublattice_parameters_double[5] = SubLat[kkk].sublattice_table_yzt_size;
                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, (void*)SubLat[k].psublattice_parameters_double);
            }

if(kkk != k){
            ptr = SubLat[kkk].GPU0->buffer_map_void(SubLat[kkk].sublattice_Lr2);
            copy_size = SubLat[kkk].size_sublattice_Lr2 * sizeof(cl_double4);

            SubLat[k].psublattice_aux = (cl_double4*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_aux);

            memcpy(SubLat[k].psublattice_aux, ptr, copy_size);
            SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_aux, SubLat[k].psublattice_aux);
        SubLat[kkk].GPU0->buffer_unmap(SubLat[kkk].sublattice_Lr2, ptr);
}

            SubLat[k].GPU0->GPU_kernels[SubLat[k].sun_measurement_WLx2_id].argument_id = 0;
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx2_id, SubLat[k].sublattice_WL_x);

            buff_id = SubLat[kkk].sublattice_Lr2;
if(kkk != k)
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx2_id, SubLat[k].sublattice_aux);
else
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx2_id, SubLat[k].sublattice_Lr2);

                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx2_id, SubLat[k].sublattice_m);
                argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx2_id, SubLat[k].sublattice_parameters);

            SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_WLx2_id);
            SubLat[k].GPU0->print_stage("measurement WLx2 done");
            
            Nxx += SubLat[kkk].Nx;
        }

        Nxx = SubLat[k].Nx;
        for(int kk = k + 1; kk <= M; kk++){
            kkk = kk % lattice_Nparts;

            if(precision == model_precision_single){
                SubLat[k].psublattice_parameters_float = (cl_float*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
                SubLat[k].psublattice_parameters_float[3] = kk;
                SubLat[k].psublattice_parameters_float[4] = Nxx;
                SubLat[k].psublattice_parameters_float[5] = SubLat[kk % lattice_Nparts].sublattice_Lt_rowsize;
                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, (void*)SubLat[k].psublattice_parameters_float);
            } else {
                SubLat[k].psublattice_parameters_double = (cl_double*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
                SubLat[k].psublattice_parameters_double[3] = kk;
                SubLat[k].psublattice_parameters_double[4] = Nxx;
                SubLat[k].psublattice_parameters_double[5] = SubLat[kk % lattice_Nparts].sublattice_Lt_rowsize;
                SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, (void*)SubLat[k].psublattice_parameters_double);
            }

if(kkk != k){
            ptr = SubLat[kkk].GPU0->buffer_map_void(SubLat[kkk].sublattice_Lt);
            copy_size = SubLat[kkk].size_sublattice_Lt * sizeof(cl_double4);
            SubLat[k].psublattice_aux = (cl_double4*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_aux);
            memcpy(SubLat[k].psublattice_aux, ptr, copy_size);
            SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_aux, SubLat[k].psublattice_aux);
        SubLat[kkk].GPU0->buffer_unmap(SubLat[kkk].sublattice_Lt, ptr);
}

            SubLat[k].GPU0->GPU_kernels[SubLat[k].sun_measurement_WLx3_id].argument_id = 0;

            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx3_id, SubLat[k].sublattice_WL_x);
            buff_id = SubLat[kkk].sublattice_Lt;
if(kkk != k)
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx3_id, SubLat[k].sublattice_aux);
else
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx3_id, SubLat[k].sublattice_Lt);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx3_id, SubLat[k].sublattice_m);
            argument_id = SubLat[k].GPU0->kernel_init_buffer(SubLat[k].sun_measurement_WLx3_id, SubLat[k].sublattice_parameters);

            SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_WLx3_id);
            SubLat[k].GPU0->print_stage("measurement WLx3 done");
           
            Nxx += SubLat[kk % lattice_Nparts].Nx;
        }
    }
#ifdef USE_OPENMP
                    }
#endif
    
#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(k = 0; k < lattice_Nparts; k++){
#endif
        
        SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_WLx4_id);
        SubLat[k].GPU0->print_stage("measurement WLx4 done");
    }

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for(k = 0; k < lattice_Nparts; k++){
#endif
        if(precision == model_precision_single){
            SubLat[k].psublattice_parameters_float = (cl_float*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
            SubLat[k].psublattice_parameters_float[3]   = (float) (wilson_R);
            SubLat[k].psublattice_parameters_float[4]   = (float) (wilson_T);
        SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, SubLat[k].psublattice_parameters_float);
        } else {
        SubLat[k].psublattice_parameters_double = (cl_double*)SubLat[k].GPU0->buffer_map_void(SubLat[k].sublattice_parameters);
            SubLat[k].psublattice_parameters_double[3]   = (double) (wilson_R);
            SubLat[k].psublattice_parameters_double[4]   = (double) (wilson_T);
        SubLat[k].GPU0->buffer_unmap(SubLat[k].sublattice_parameters, SubLat[k].psublattice_parameters_double);
        }

        SubLat[k].GPU0->kernel_run(SubLat[k].sun_measurement_wilson_id);       // Lattice Wilson loop measurement
        SubLat[k].GPU0->print_stage("Wilson loop measurement done");

            SubLat[k].GPU0->kernel_init_constant_reset(SubLat[k].sun_wilson_loop_reduce_id, &wilson_index, SubLat[k].argument_wilson_index);

        SubLat[k].GPU0->kernel_run(SubLat[k].sun_wilson_loop_reduce_id);       // Lattice Wilson loop measurement reduction
        SubLat[k].GPU0->print_stage("Wilson loop measurement reduce done");
    }
}
#endif

#ifdef CHB2
void        model::lattice_simulate(void)
{
            lattice_create_buffers();
            lattice_make_programs();

            int NAV_start  = 0;
            int ITER_start = 0;

            int kk;
            int n1n2n3;

            time_t timer1, timer2;
            time(&timer1);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                printf("\n");
                SubLat[k].GPU0->print_memory_utilized();

                SubLat[k].timestart = SubLat[k].GPU0->get_current_datetime();
                time(&(SubLat[k].ltimestart));

                printf("\nrun kernels on GPU %i (%f seconds)\n", f, SubLat[k].GPU0->get_timer_CPU(TIMER_FOR_ELAPSED));
                SubLat[k].GPU0->start_timer_CPU(TIMER_FOR_SIMULATIONS); // start GPU execution timer
                SubLat[k].GPU0->start_timer_CPU(TIMER_FOR_SAVE);        // start timer for lattice_state save

                for(int g = 0; g < 2; g++)//skip 3 first numbers
                    for(int k = 0; k < lattice_Nparts; k++)
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (ints==model_start_hot) 
                    for(int k = 0; k < lattice_Nparts; k++)
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
            }

            if (INIT!=0) {
#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (ints==model_start_hot) {
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_X_id);        // Lattice initialization
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_Y_id);        // Lattice initialization
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_Z_id);        // Lattice initialization
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_T_id);        // Lattice initialization
                    } else
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_id);          // Lattice initialization
                }
                if (ints==model_start_hot){
                    UpdateEdges(X);
                    UpdateEdges(Y);
                    UpdateEdges(Z);
                    UpdateEdges(T);
                }

                //if (!turnoff_gramschmidt)
                //        for(int k = 0; k < lattice_Nparts; k++)
                //            SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_gramschmidt)
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);

                    SubLat[k].GPU0->print_stage("lattice initialized");

#ifdef BIGTOSMALL
                    SubLat[k].GPU0->kernel_run(SubLat[k].sun_tables_id);
#endif

                    if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) 
                        SubLat[k].sublattice_Measure_Plq(ITER_counter);
                    if (get_actions_avr) 
                        SubLat[k].sublattice_Measure_Action(ITER_counter);
                    if (get_actions_diff) 
                        SubLat[k].sublattice_Measure_Action_diff(ITER_counter);
                    if (PL_level > 0)
                        SubLat[k].sublattice_Measure_PL(ITER_counter);
                    if (PL_level > 2) {
                        SubLat[k].sublattice_Measure_PL_diff(ITER_counter);
                    }

                    //if (!turnoff_config_save) lattice_save_state();

                    SubLat[k].sublattice_pointer_initial = SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table);
                }
                if (get_wilson_loop){
                    wilson_index = ITER_counter;
                    lattice_make_WLx();
                }
                ITER_counter++;
                    }

            //--- Simulate ----------------------------
            NAV_start  = NAV_counter;
            ITER_start = ITER_counter;

            //if (INIT==0) {
            //    while (PRNG_counter>PRNG0->PRNG_counter) PRNG0->produce();    // adjust PRNG
            //    if (GPU0->GPU_debug.brief_report) printf("NAV_start=%u, ITER_start=%u\n",NAV_start,ITER_start);

            //    wilson_index = ITER_start + 1;
            //}

            // perform thermalization
            for (int i=NAV_start; i<NAV; i++){
#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_X_id);     // Update odd X links
                }
                if (!turnoff_updates)
                    UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Y_id);     // Update odd Y links
                }
                if (!turnoff_updates)
                    UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Z_id);     // Update odd Z links
                }
                if (!turnoff_updates)
                    UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_T_id);     // Update odd T links
                }
                if (!turnoff_updates)
                    UpdateEdges(T);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_X_id);    // Update even X links
                }
                if (!turnoff_updates)
                    UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Y_id);    // Update even Y links
                }
                if (!turnoff_updates)
                    UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Z_id);    // Update even Z links
                }
                if (!turnoff_updates)
                    UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_T_id);    // Update even T links
                }
                if (!turnoff_updates)
                    UpdateEdges(T);

                if (!turnoff_gramschmidt)
                    for (int k = 0; k < lattice_Nparts; k++)
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);          // Lattice reunitarization

                if (i % 10 == 0) printf("\rGPU thermalization [%i]",i);
                NAV_counter++;
                    }

            // perform working cycles
            int q;
            for (int i=ITER_start; i<ITER; i++){ // zero measurement - on initial configuration!
                for (int j=0; j<NITER; j++){
//#ifdef USE_OPENMP
                    for (int kk = 0; kk < devParts[0]; kk++){
#pragma omp parallel for
                        for (int f = 0; f < Ndevices; f++){
                            int k = kk + devLeftParts[f];
//#else
//					for (int k = 0; k < lattice_Nparts; k++){
//#endif
                        if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_X_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Y_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Z_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_T_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_X_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Y_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Z_id);

                                                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                        if (!turnoff_updates)
                            SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_T_id);
                        }
#pragma omp barrier
                        if (!turnoff_updates){
                            UpdateEdges(X);
                            UpdateEdges(Y);
                            UpdateEdges(Z);
                            UpdateEdges(T);
                        }
                    }
                        }

                if (!turnoff_gramschmidt)
#ifdef USE_OPENMP
#pragma omp parallel for
                    for (int f = 0; f < Ndevices; f++)
                        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++)
#else
                    for(int k = 0; k < lattice_Nparts; k++)
#endif
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);

                if (i % 10 == 0) printf("\rGPU working iteration [%u]",i);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
#ifdef BIGTOSMALL
                    SubLat[k].GPU0->kernel_run(SubLat[k].sun_tables_id);
#endif
                    if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) 
                        SubLat[k].sublattice_Measure_Plq(ITER_counter);
                    if (get_actions_avr) 
                        SubLat[k].sublattice_Measure_Action(ITER_counter);
                    if (get_actions_diff) 
                        SubLat[k].sublattice_Measure_Action_diff(ITER_counter);
                    if (PL_level > 0)
                        SubLat[k].sublattice_Measure_PL(ITER_counter);
                    if (PL_level > 2) 
                        SubLat[k].sublattice_Measure_PL_diff(ITER_counter);
                }
                if (get_wilson_loop){
                    wilson_index = ITER_counter;
                    lattice_make_WLx();
                }

                ITER_counter++;
                    }

            //-----------------------------------------
            time(&timer2);
            printf("\nSIMULATION TIME: %f sec\n", difftime(timer2, timer1));

            for (int k = 0; k < lattice_Nparts; k++)
            {
                printf("\rGPU simulations are done (%f seconds)\n",SubLat[k].GPU0->get_timer_CPU(1));
                time(&(SubLat[k].ltimeend));
                SubLat[k].timeend   = SubLat[k].GPU0->get_current_datetime();

                if (!turnoff_config_save) {
                    //lattice_save_state();
                    //lattice_pointer_last = lattice_pointer_save;
                } else {
                    SubLat[k].sublattice_pointer_last = SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table);
                }
                //prng_pointer = GPU0->buffer_map_float4(PRNG0->PRNG_randoms_id);
            }

            timestart = SubLat[0].timestart;
            timeend = SubLat[0].timeend;
            ltimestart = SubLat[0].ltimestart;
            ltimeend = SubLat[0].ltimeend;
            for(int k = 1; k < lattice_Nparts; k++){
                if(SubLat[k].timestart < timestart) timestart = SubLat[k].timestart;
                if(SubLat[k].timeend > timeend) timeend = SubLat[k].timeend;
                if(SubLat[k].ltimestart< ltimestart) ltimestart = SubLat[k].ltimestart;
                if(SubLat[k].ltimeend > ltimeend) ltimeend = SubLat[k].ltimeend;
            }
                        }
#else
void        model::lattice_simulate(void)
{
    lattice_create_buffers();
    lattice_make_programs();

    int NAV_start  = 0;
    int ITER_start = 0;

    int kk;
    int n1n2n3;

    time_t timer1, timer2;
    time(&timer1);

#ifdef USE_OPENMP
#pragma omp parallel for
    for (int f = 0; f < Ndevices; f++)
        for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
    for (int k = 0; k < lattice_Nparts; k++){
#endif
        printf("\n");
        SubLat[k].GPU0->print_memory_utilized();

        SubLat[k].timestart = SubLat[k].GPU0->get_current_datetime();
        time(&(SubLat[k].ltimestart));

        printf("\nrun kernels on GPU (%f seconds)\n", SubLat[k].GPU0->get_timer_CPU(TIMER_FOR_ELAPSED));
        SubLat[k].GPU0->start_timer_CPU(TIMER_FOR_SIMULATIONS); // start GPU execution timer
        SubLat[k].GPU0->start_timer_CPU(TIMER_FOR_SAVE);        // start timer for lattice_state save

        for(int g = 0; g < 2; g++)//skip 3 first numbers
            for(int k = 0; k < lattice_Nparts; k++)
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
        if (ints==model_start_hot) 
            for(int k = 0; k < lattice_Nparts; k++)
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
    }

    if (INIT!=0) {
#ifdef USE_OPENMP
#pragma omp parallel for
        for (int f = 0; f < Ndevices; f++)
            for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
        for (int k = 0; k < lattice_Nparts; k++){
#endif
            if (ints==model_start_hot) {
            if (!turnoff_prns) SubLat[k].PRNG0->produce();
                SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_X_id);        // Lattice initialization
            if (!turnoff_prns) SubLat[k].PRNG0->produce();
            SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_Y_id);        // Lattice initialization
            if (!turnoff_prns) SubLat[k].PRNG0->produce();
            SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_Z_id);        // Lattice initialization
            if (!turnoff_prns) SubLat[k].PRNG0->produce();
            SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_T_id);        // Lattice initialization
            } else
                SubLat[k].GPU0->kernel_run(SubLat[k].sun_init_id);          // Lattice initialization
        }
        if (ints==model_start_hot){
            UpdateEdges(X);
            UpdateEdges(Y);
            UpdateEdges(Z);
            UpdateEdges(T);
        }
        
#ifdef USE_OPENMP
#pragma omp parallel for
        for (int f = 0; f < Ndevices; f++)
            for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
        for (int k = 0; k < lattice_Nparts; k++){
#endif
            if (!turnoff_gramschmidt)
                SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);

            SubLat[k].GPU0->print_stage("lattice initialized");

#ifdef BIGTOSMALL
            SubLat[k].GPU0->kernel_run(SubLat[k].sun_tables_id);
#endif

            if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) 
                    SubLat[k].sublattice_Measure_Plq(ITER_counter);
            if (get_actions_avr) 
                    SubLat[k].sublattice_Measure_Action(ITER_counter);
            if (get_actions_diff) 
                    SubLat[k].sublattice_Measure_Action_diff(ITER_counter);
            if (PL_level > 0)
                SubLat[k].sublattice_Measure_PL(ITER_counter);
            if (PL_level > 2) {
                SubLat[k].sublattice_Measure_PL_diff(ITER_counter);
            }
        }
        if (get_wilson_loop){
            wilson_index = ITER_counter;
            lattice_make_WLx();
        }
        ITER_counter++;
    }
    
        //--- Simulate ----------------------------
        NAV_start  = NAV_counter;
        ITER_start = ITER_counter;

        // perform thermalization
        for (int i=NAV_start; i<NAV; i++){
#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_X_id);     // Update odd X links
            }
            if (!turnoff_updates)
                UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Y_id);     // Update odd Y links
            }
            if (!turnoff_updates)
                UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Z_id);     // Update odd Z links
            }
            if (!turnoff_updates)
                UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_T_id);     // Update odd T links
            }
            if (!turnoff_updates)
                UpdateEdges(T);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_X_id);    // Update even X links
            }
            if (!turnoff_updates)
                UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Y_id);    // Update even Y links
            }
            if (!turnoff_updates)
                UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Z_id);    // Update even Z links
            }
            if (!turnoff_updates)
                UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                if (!turnoff_prns) SubLat[k].PRNG0->produce();
                if (!turnoff_updates) SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_T_id);    // Update even T links
            }
            if (!turnoff_updates)
                UpdateEdges(T);

            if (!turnoff_gramschmidt)
                for (int k = 0; k < lattice_Nparts; k++)
                    SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);          // Lattice reunitarization

        if (i % 10 == 0) printf("\rGPU thermalization [%i]",i);
        NAV_counter++;
        }
        
        // perform working cycles
        int q;
        for (int i=ITER_start; i<ITER; i++){ // zero measurement - on initial configuration!
            for (int j=0; j<NITER; j++){
#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_X_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Y_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_Z_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_odd_T_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(T);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_X_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(X);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Y_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(Y);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_Z_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(Z);

#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
                for (int k = 0; k < lattice_Nparts; k++){
#endif
                    if (!turnoff_prns) SubLat[k].PRNG0->produce();
                    if (!turnoff_updates) 
                        SubLat[k].GPU0->kernel_run(SubLat[k].sun_update_even_T_id);
                }
                if (!turnoff_updates)
                    UpdateEdges(T);
            }

            if (!turnoff_gramschmidt)
#ifdef USE_OPENMP
#pragma omp parallel for
                for (int f = 0; f < Ndevices; f++)
                    for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++)
#else
                for(int k = 0; k < lattice_Nparts; k++)
#endif
                    SubLat[k].GPU0->kernel_run(SubLat[k].sun_GramSchmidt_id);

            if (i % 10 == 0) printf("\rGPU working iteration [%u]",i);

#ifdef USE_OPENMP
#pragma omp parallel for
            for (int f = 0; f < Ndevices; f++)
                for (int k = devLeftParts[f]; k < devLeftParts[f] + devParts[f]; k++){
#else
            for (int k = 0; k < lattice_Nparts; k++){
#endif
                
                if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) 
                        SubLat[k].sublattice_Measure_Plq(ITER_counter);
                if (get_actions_avr) 
                        SubLat[k].sublattice_Measure_Action(ITER_counter);
                if (get_actions_diff) 
                    SubLat[k].sublattice_Measure_Action_diff(ITER_counter);
                if (PL_level > 0)
                    SubLat[k].sublattice_Measure_PL(ITER_counter);
                if (PL_level > 2) 
                    SubLat[k].sublattice_Measure_PL_diff(ITER_counter);
            }
            if (get_wilson_loop){
                wilson_index = ITER_counter;
                lattice_make_WLx();
            }

            ITER_counter++;
        }

    //-----------------------------------------
    time(&timer2);
    printf("\nSIMULATION TIME: %f sec\n", difftime(timer2, timer1));

    for (int k = 0; k < lattice_Nparts; k++)
    {
        printf("\rGPU simulations are done (%f seconds)\n",SubLat[k].GPU0->get_timer_CPU(1));
        time(&(SubLat[k].ltimeend));
        SubLat[k].timeend   = SubLat[k].GPU0->get_current_datetime();

        if (!turnoff_config_save) {
            //lattice_save_state();
            //lattice_pointer_last = lattice_pointer_save;
        } //else {
            //SubLat[k].sublattice_pointer_last = SubLat[k].GPU0->buffer_map(SubLat[k].sublattice_table);
        //}

        //prng_pointer = GPU0->buffer_map_float4(PRNG0->PRNG_randoms_id);
    }

    timestart = SubLat[0].timestart;
    timeend = SubLat[0].timeend;
    ltimestart = SubLat[0].ltimestart;
    ltimeend = SubLat[0].ltimeend;
    for(int k = 1; k < lattice_Nparts; k++){
        if(SubLat[k].timestart < timestart) timestart = SubLat[k].timestart;
        if(SubLat[k].timeend > timeend) timeend = SubLat[k].timeend;
        if(SubLat[k].ltimestart< ltimestart) ltimestart = SubLat[k].ltimestart;
        if(SubLat[k].ltimeend > ltimeend) ltimeend = SubLat[k].ltimeend;
    }
}
#endif
#else
void        model::lattice_simulate(void)
{
    lattice_create_buffers();
    lattice_make_programs();

    rowsize      = lattice_table_row_size;
    rowsize4     = 4 * lattice_table_row_size;
    halfrowsize  = lattice_domain_link / 8;
    halfrowsize4 = lattice_domain_link / 2;


    // simulations ______________________________________________________________________________________________________________________________________________
    GPU0->print_memory_utilized();

    timestart = GPU0->get_current_datetime();
    time(&ltimestart);

    lattice_pointer_initial      = NULL;
    lattice_pointer_measurements = NULL;

    printf("\nrun kernels on GPU (%f seconds)\n",GPU0->get_timer_CPU(TIMER_FOR_ELAPSED));
    GPU0->start_timer_CPU(TIMER_FOR_SIMULATIONS); // start GPU execution timer
    GPU0->start_timer_CPU(TIMER_FOR_SAVE);        // start timer for lattice_state save

    if (ints==model_start_hot) PRNG0->produce();

    int NAV_start  = 0;
    int ITER_start = 0;

    if (INIT!=0) {

        if (ints==model_start_hot) {
            GPU0->kernel_run(sun_init_X_id);        // Lattice initialization
            if (!turnoff_prns) PRNG0->produce();
            GPU0->kernel_run(sun_init_Y_id);        // Lattice initialization
            if (!turnoff_prns) PRNG0->produce();
            GPU0->kernel_run(sun_init_Z_id);        // Lattice initialization
            if (!turnoff_prns) PRNG0->produce();
            GPU0->kernel_run(sun_init_T_id);        // Lattice initialization
        } else
            GPU0->kernel_run(sun_init_id);          // Lattice initialization
        GPU0->print_stage("lattice initialized");

        if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) {
            GPU0->kernel_run(sun_measurement_plq_id);          // Lattice measurement (plaquettes)
            GPU0->print_stage("measurement done (plaquettes)");
//         GPU0->buffer_map(lattice_measurement);
//         GPU0->print_mapped_buffer_double2(lattice_measurement,5);
                plq_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_measurement_plq_reduce_id,&plq_index,argument_plq_index);
            GPU0->kernel_run(sun_measurement_plq_reduce_id);    // Lattice measurement reduction (plaquettes)
            GPU0->print_stage("measurement reduce done (plaquettes)");
        }
    
        if (get_wilson_loop) {
            GPU0->kernel_run(sun_measurement_wilson_id);       // Lattice Wilson loop measurement
            GPU0->print_stage("Wilson loop measurement done");
                wilson_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_wilson_loop_reduce_id,&wilson_index,argument_wilson_index);
            GPU0->kernel_run(sun_wilson_loop_reduce_id);       // Lattice Wilson loop measurement reduction
            GPU0->print_stage("Wilson loop measurement reduce done");
        }

        if (get_actions_avr) {
            GPU0->kernel_run(sun_measurement_id);                  // Lattice measurement
            GPU0->print_stage("measurement done");
                measurement_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_measurement_reduce_id,&measurement_index,argument_measurement_index);
            GPU0->kernel_run(sun_measurement_reduce_id);           // Lattice measurement reduction
            GPU0->print_stage("measurement reduce done");
        }

        if(get_actions_diff)
    {
        GPU0->kernel_run(sun_action_diff_x_id);
        GPU0->print_stage("measurement S diff done");
                action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_x_reduce_id,&action_diff_index,argument_action_diff_x_index);
            GPU0->kernel_run(sun_action_diff_x_reduce_id);
            GPU0->print_stage("measurement action diff reduce done");
        
        GPU0->kernel_run(sun_action_diff_y_id);
        GPU0->print_stage("measurement S diff done");
                action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_y_reduce_id,&action_diff_index,argument_action_diff_y_index);
            GPU0->kernel_run(sun_action_diff_y_reduce_id);
            GPU0->print_stage("measurement action diff reduce done");
        
        GPU0->kernel_run(sun_action_diff_z_id);
        GPU0->print_stage("measurement S diff done");
                action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_z_reduce_id,&action_diff_index,argument_action_diff_z_index);
            GPU0->kernel_run(sun_action_diff_z_reduce_id);
            GPU0->print_stage("measurement action diff reduce done");
    }
        
        if (PL_level > 0) {
            GPU0->kernel_run(sun_polyakov_id);                     // Lattice Polyakov loop measurement
            GPU0->print_stage("Polyakov loop measurement done");
                polyakov_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_reduce_id,&polyakov_index,argument_polyakov_index);
            GPU0->kernel_run(sun_polyakov_reduce_id);              // Lattice Polyakov loop measurement reduction
            GPU0->print_stage("Polyakov loop reduce done");
        }
        
        if (PL_level > 2) {
            GPU0->kernel_run(sun_polyakov_diff_x_id);                     // Lattice Polyakov loop measurement
            GPU0->print_stage("Polyakov loop diff measurement done");
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_x_reduce_id,&polyakov_diff_index,argument_polyakov_diff_x_index);
            GPU0->kernel_run(sun_polyakov_diff_x_reduce_id);              // Lattice Polyakov loop measurement reduction
            GPU0->print_stage("Polyakov loop diff reduce done");
        
        GPU0->kernel_run(sun_polyakov_diff_y_id);                     // Lattice Polyakov loop measurement
            GPU0->print_stage("Polyakov loop diff measurement done");
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_y_reduce_id,&polyakov_diff_index,argument_polyakov_diff_y_index);
            GPU0->kernel_run(sun_polyakov_diff_y_reduce_id);              // Lattice Polyakov loop measurement reduction
            GPU0->print_stage("Polyakov loop diff reduce done");
        
        GPU0->kernel_run(sun_polyakov_diff_z_id);                     // Lattice Polyakov loop measurement
            GPU0->print_stage("Polyakov loop diff measurement done");
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_z_reduce_id,&polyakov_diff_index,argument_polyakov_diff_z_index);
            GPU0->kernel_run(sun_polyakov_diff_z_reduce_id);              // Lattice Polyakov loop measurement reduction
            GPU0->print_stage("Polyakov loop diff reduce done");
        }

        ITER_counter++;

        if (!turnoff_config_save) lattice_save_state();
    }
    lattice_pointer_initial = GPU0->buffer_map(lattice_table);

    NAV_start  = NAV_counter;
    ITER_start = ITER_counter;

    if (INIT==0) {
        while (PRNG_counter>PRNG0->PRNG_counter) PRNG0->produce();    // adjust PRNG
        if (GPU0->GPU_debug.brief_report) printf("NAV_start=%u, ITER_start=%u\n",NAV_start,ITER_start);

        wilson_index = ITER_start + 1;
    }

    // perform thermalization
    for (int i=NAV_start; i<NAV; i++){
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_X_id);     // Update odd X links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_Y_id);     // Update odd Y links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_Z_id);     // Update odd Z links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_T_id);     // Update odd T links

           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_even_X_id);    // Update even X links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_even_Y_id);    // Update even Y links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_even_Z_id);    // Update even Z links
           if (!turnoff_prns) PRNG0->produce();
        if (!turnoff_updates) GPU0->kernel_run(sun_update_even_T_id);    // Update even T links

            if (!turnoff_gramschmidt)
                GPU0->kernel_run(sun_GramSchmidt_id);          // Lattice reunitarization

        if (i % 10 == 0) printf("\rGPU thermalization [%i]",i);
        NAV_counter++;

        // write lattice state every [write_lattice_state_every_secs] seconds
        if ((!turnoff_config_save)&&(GPU0->timer_in_seconds_CPU(TIMER_FOR_SAVE)>write_lattice_state_every_secs)) {
            lattice_save_state();
            printf("\ncurrent configuration saved\n");
            GPU0->start_timer_CPU(TIMER_FOR_SAVE);      // restart timer for lattice_state save
        } 
    }

    // perform working cycles
    for (int i=ITER_start; i<ITER; i++){ // zero measurement - on initial configuration!
        for (int j=0; j<NITER; j++){
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_X_id);     // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_Y_id);     // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_Z_id);     // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_odd_T_id);     // Lattice measurement staples

               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_even_X_id);    // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_even_Y_id);    // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_even_Z_id);    // Lattice measurement staples
               if (!turnoff_prns) PRNG0->produce();
            if (!turnoff_updates) GPU0->kernel_run(sun_update_even_T_id);    // Lattice measurement staples
            
            if (!turnoff_gramschmidt)
                GPU0->kernel_run(sun_GramSchmidt_id);           // Lattice reunitarization

            if (i % 10 == 0) printf("\rGPU working iteration [%u]",i);
        }

        if (get_wilson_loop) {
            GPU0->kernel_run(sun_measurement_wilson_id);        // Lattice Wilson loop measurement
                wilson_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_wilson_loop_reduce_id,&wilson_index,argument_wilson_index);
            GPU0->kernel_run(sun_wilson_loop_reduce_id);        // Lattice Wilson loop measurement reduction
        }

        // measurements
        if (get_actions_avr) {
            GPU0->kernel_run(sun_measurement_id);                 // Lattice measurement
                measurement_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_measurement_reduce_id,&measurement_index,argument_measurement_index);
            GPU0->kernel_run(sun_measurement_reduce_id);          // Lattice measurement reduction
        }
        
        if(get_actions_diff)
    {
        GPU0->kernel_run(sun_action_diff_x_id);
        action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_x_reduce_id, &action_diff_index, argument_action_diff_x_index);
            GPU0->kernel_run(sun_action_diff_x_reduce_id);
        
        GPU0->kernel_run(sun_action_diff_y_id);
        action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_y_reduce_id, &action_diff_index, argument_action_diff_y_index);
            GPU0->kernel_run(sun_action_diff_y_reduce_id);
        
        GPU0->kernel_run(sun_action_diff_z_id);
        action_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_action_diff_z_reduce_id, &action_diff_index, argument_action_diff_z_index);
            GPU0->kernel_run(sun_action_diff_z_reduce_id);
    }

GPU0->kernel_run(sun_clear_measurement_id);
        if (PL_level > 0) {
            GPU0->kernel_run(sun_polyakov_id);                    // Lattice Polyakov loop measurement
                polyakov_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_reduce_id,&polyakov_index,argument_polyakov_index);
            GPU0->kernel_run(sun_polyakov_reduce_id);             // Lattice Polyakov loop measurement reduction
        }
        
GPU0->kernel_run(sun_clear_measurement_id);
        if (PL_level > 2) {
            GPU0->kernel_run(sun_polyakov_diff_x_id);
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_x_reduce_id,&polyakov_diff_index,argument_polyakov_diff_x_index);
            GPU0->kernel_run(sun_polyakov_diff_x_reduce_id);
        
        GPU0->kernel_run(sun_polyakov_diff_y_id);
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_y_reduce_id,&polyakov_diff_index,argument_polyakov_diff_y_index);
            GPU0->kernel_run(sun_polyakov_diff_y_reduce_id);
        
        GPU0->kernel_run(sun_polyakov_diff_z_id);
                polyakov_diff_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_polyakov_diff_z_reduce_id,&polyakov_diff_index,argument_polyakov_diff_z_index);
            GPU0->kernel_run(sun_polyakov_diff_z_reduce_id);
        }

GPU0->kernel_run(sun_clear_measurement_id);
        // measurements (plaquettes)
        if ((get_plaquettes_avr)||(get_Fmunu)||(get_F0mu)) {
            GPU0->kernel_run(sun_measurement_plq_id);           // Lattice measurement (plaquettes)
                plq_index = ITER_counter;
                GPU0->kernel_init_constant_reset(sun_measurement_plq_reduce_id,&plq_index,argument_plq_index);
            GPU0->kernel_run(sun_measurement_plq_reduce_id);    // Lattice measurement reduction (plaquettes)
        }

        ITER_counter++;

        // write lattice state every [write_lattice_state_every_secs] seconds
        if ((!turnoff_config_save)&&(GPU0->timer_in_seconds_CPU(TIMER_FOR_SAVE)>write_lattice_state_every_secs)) {
            lattice_save_state();
            printf("\ncurrent configuration saved\n");
            GPU0->start_timer_CPU(TIMER_FOR_SAVE);      // restart timer for lattice_state save
        } 
    }
    printf("\rGPU simulations are done (%f seconds)\n",GPU0->get_timer_CPU(1));
    time(&ltimeend);
    timeend   = GPU0->get_current_datetime();

    if (!turnoff_config_save) {
        lattice_save_state();
        lattice_pointer_last = lattice_pointer_save;
    } else {
        lattice_pointer_last = GPU0->buffer_map(lattice_table);
    }
    prng_pointer = GPU0->buffer_map_float4(PRNG0->PRNG_randoms_id);
}
#endif
#endif//CPU_RUN
}

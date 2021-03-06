/*
 * Copyright (C) 1999-2014 Anthony Lomax <anthony@alomax.net, http://www.alomax.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


/*   NLDiffLoc.c

        Program to do global search earthquake location in 3-D models

 */

/*-----------------------------------------------------------------------
Anthony Lomax
ALomax Scientific
e-mail: anthony@alomax.net  web: http://www.alomax.net
-------------------------------------------------------------------------*/


/*
        history:

        ver 01    27JUL2004  AJL  Original version
        ver 01    15MAY2014  AJL  Added L1 norm


.........1.........2.........3.........4.........5.........6.........7.........8

 */


/* References */
/*
        TV82	Tarantola and Valette,  (1982)
                "Inverse Problems = Quest for Information",
                J Geophys 50, 159-170.
        MEN92	Moser, van Eck and Nolet,  (1992)
                "Hypocenter Determination ... Shortest Path Method",
                JGR 97, B5, 6563-6572.
 */





#include "GridLib.h"
#include "ran1/ran1.h"
#include "velmod.h"
#include "GridMemLib.h"
#include "calc_crust_corr.h"
#include "phaseloclist.h"
#include "otime_limit.h"
#include "NLLocLib.h"

#define PNAME  "NLDiffLoc"




/*------------------------------------------------------------*/
/* globals  */
EXTERN_TXT char fn_hypocenters[FILENAME_MAX];
EXTERN_TXT char ftype_hypocenters[MAXLINE];
EXTERN_TXT int NumHypocenterFix;
EXTERN_TXT int NumHypocenterFree;
#define  MAX_NUM_DIFF_HYPOCENTERS 1000
EXTERN_TXT HypoDesc DiffHypocenters[MAX_NUM_DIFF_HYPOCENTERS];
EXTERN_TXT int NumHypocenters;
EXTERN_TXT double xcorr_uncertainty;
EXTERN_TXT double cat_uncertainty;



/*------------------------------------------------------------*/
/* function declarations  */
int ReadNLDiffLoc_Input(FILE* fp_input);
int GetNLDiffLoc_HypFile(char* line1);
int GetNLDiffLoc_SearchType(char* line1);
int GetHypocenters(char* fn_hypos, char* ftype_hypos, HypoDesc* Hypos, int max_num_hypos);
int AssignEventIndexes(int num_hypos, HypoDesc* hypos, int num_arrivals, ArrivalDesc *arrival, double *, int *pnumberP, int *pnumberS, int *pnumberOther);
int LocateDiff(char* fn_obs, char* fn_path_output, int numArrivalsReject);
int DiffLocMetropolis(int num_arr_total, int num_arr_loc,
        ArrivalDesc *arrival,
        GridDesc* ptgrid, GaussLocParams* gauss_par,
        WalkParams* pMetrop, float* fdata);
int DiffLocGetNextMetropolisSample(WalkParams* pMetrop, double dx, double xmin, double xmax,
        double ymin, double ymax, double zmin, double zmax,
        double* pxval, double* pyval, double* pzval, double* ptval);
double DiffLocCalcSolutionQuality(
        Vect3D hypo_test, double dtime,
        int nHypo, int num_hypos, HypoDesc* hypos,
        int num_arrivals, ArrivalDesc* arrival,
        GaussLocParams* gauss_par, int itype, double temperature, double* pmisfit, double* potime, int* pnReject, int);
double DiffLocCalcSolutionQuality_LN_NORM(double norm,
        Vect3D hypo_test, double dtime,
        int nHypo, int num_hypos, HypoDesc* hypos,
        int num_arrivals, ArrivalDesc* arrival,
        GaussLocParams* gauss_par, int itype, double temperature, double* pmisfit, double* potime, int* pnReject, int);
double getTravelTimeDiff(ArrivalDesc* arrival, int narr, Vect3D hypo1, Vect3D hypo2);
int DiffLocSaveBestLocation(int num_arr_total, int num_arr_loc, ArrivalDesc *arrival,
        GridDesc* ptgrid, GaussLocParams* gauss_par, int nHypo, int iGridType);
int DiffLocMetropolisTest(double value_last, double value_new, double exp_last, double exp_new);
int SaveDiffTimeLinks(int num_hypos, HypoDesc* hypos, int num_arrivals, ArrivalDesc* arrival, FILE * fp_out);
int SaveHypoDDRes(int num_hypos, HypoDesc* hypos, int num_arrivals, ArrivalDesc* arrival, FILE* fp_out);

/*** program to perform global search differential event locations */

#define NARGS_MIN 2
#define ARG_DESC "<control file>"

int main(int argc, char *argv[]) {

    int istat, n;
    int i_end_of_input, iLocated = 0;
    int nhyp, narr, ngrid, nObsFile;
    int numArrivalsIgnore, numSArrivalsLocation;
    int numHypocentersAssigned;
    int numArrivalsReject;
    int numArrivalsNew;
    int numArrivalsIgnoreNew;
    int numArrivalsRejectNew;
    int numSArrivalsLocNew;
    int maxArrExceeded = 0;
    int nIgnored;
    char fname[FILENAME_MAX];
    char sys_command[MAXLINE_LONG];
    char *chr;
    double mean_abs;
    FILE *fp_obs, *fp_out;

    char *ppath;



    /* set program name */
    strcpy(prog_name, PNAME);

    /* check command line for correct usage */

    if (argc < NARGS_MIN) {
        disp_usage(prog_name, ARG_DESC);
        exit(EXIT_ERROR_USAGE);
    }


    // DD
    nll_mode = MODE_DIFFERENTIAL;

    /* set constants */
    SetConstants();

    // re-allocate arrivals arrays
    // allocate arrivals array
    MAX_NUM_STATIONS = X_MAX_NUM_STATIONS_DIFF;
    MAX_NUM_ARRIVALS = MAX_NUM_ARRIVALS_STA * MAX_NUM_STATIONS;
    free(Arrival);
    if ((Arrival = (ArrivalDesc *) malloc(MAX_NUM_ARRIVALS * sizeof (ArrivalDesc))) == NULL) {
        nll_puterr("ERROR: re-allocating Arrival array.");
        return (EXIT_ERROR_MEMORY);
    }



    NumLocGrids = 0;
    NumEvents = NumEventsLocated = NumLocationsCompleted = 0;
    NumCompDesc = 0;
    NumLocAlias = 0;
    NumLocExclude = 0;
    NumTimeDelays = 0;
    NumPhaseID = 0;
    DistStaGridMax = 0.0;
    MinNumArrLoc = 0;
    MinNumSArrLoc = 0;
    MaxNumArrLoc = MAX_NUM_ARRIVALS;
    FixOriginTimeFlag = 0;
    Scatter.npts = -1;
    for (n = 0; n < MAX_NUM_MAG_METHODS; n++)
        Magnitude[n].type = MAG_UNDEF;
    NumMagnitudeMethods = 0;

    iSetStationDistributionWeights = 0;
    iRejectDuplicateArrivals = 0;

    // GridMemLib
    MaxNum3DGridMemory = -1;
    GridMemList = NULL;
    GridMemListSize = 0;
    GridMemListNumElements = 0;

    // GLOBAL
    NumSources = 0;

    /* open control file */
    strcpy(fn_control, argv[1]);
    if ((fp_control = fopen(fn_control, "r")) == NULL) {
        nll_puterr("FATAL ERROR: opening control file.");
        exit(EXIT_ERROR_FILEIO);
    } else {
        NumFilesOpen++;
    }
    /* read NLLoc control statements from control file */
    if ((istat = ReadNLLoc_Input(fp_control, NULL, -1)) < 0) {
        nll_puterr("FATAL ERROR: reading control file.");
        exit(EXIT_ERROR_FILEIO);
    }
    rewind(fp_control);
    /* read NLDiffLoc control statements from control file */
    if ((istat = ReadNLDiffLoc_Input(fp_control)) < 0) {
        nll_puterr("FATAL ERROR: reading control file.");
        exit(EXIT_ERROR_FILEIO);
    }
    /* close */
    fclose(fp_control);
    NumFilesOpen--;



    // get path to output files
    strcpy(f_outpath, fn_path_output);
    if ((ppath = strrchr(f_outpath, '/')) != NULL
            || (ppath = strrchr(f_outpath, '\\')) != NULL)
        *(ppath + 1) = '\0';
    else
        strcpy(f_outpath, "");


    // copy control file to output directory
    strcpy(fname, fn_control);
    chr = strrchr(fn_control, '/');
    if (chr != NULL)
        strcpy(fname, chr + 1);
    sprintf(sys_command, "cp -p %s %s_%s", fn_control, fn_path_output, fname);
    system(sys_command);
    sprintf(sys_command, "cp -p %s %slast.in", fn_control, f_outpath);
    system(sys_command);
    //printf("sys_command: %s\n", sys_command);


    /* convert source location coordinates  */
    istat = ConvertSourceLoc(0, Source, NumSources, 1, 1);


    /* initialize random number generator */

    SRAND_FUNC(RandomNumSeed);
    if (message_flag >= 4)
        test_rand_int();

    /* set uncertainty weight */
    xcorr_uncertainty = 0.01;
    if (Quality2Error[0] < SMALL_DOUBLE || Quality2Error[0] > 1000.0)
        nll_puterr("WARNING: invalid LOCQUAL2ERR value, setting xcorr error to 0.01 sec");
    else
        xcorr_uncertainty = Quality2Error[0];
    cat_uncertainty = 0.05;
    if (Quality2Error[1] < SMALL_DOUBLE || Quality2Error[1] > 1000.0)
        nll_puterr("WARNING: invalid LOCQUAL2ERR value, setting cat error to 0.05 sec");
    else
        cat_uncertainty = Quality2Error[1];


    /* read each observation file */

    NumArrivals = 0;
    numArrivalsIgnore = 0;
    numArrivalsReject = 0;
    numSArrivalsLocation = 0;
    numHypocentersAssigned = 0;


    for (nObsFile = 0; nObsFile < NumObsFiles; nObsFile++) {

        i_end_of_input = 0;

        nll_putmsg(2, "");
        sprintf(MsgStr, "... Reading observation file %s", fn_loc_obs[nObsFile]);
        nll_putmsg(1, MsgStr);

        /* open observation file */

        if ((fp_obs = fopen(fn_loc_obs[nObsFile], "r")) == NULL) {
            nll_puterr2("ERROR: opening observations file", fn_loc_obs[nObsFile]);
            continue;
        } else {
            NumFilesOpen++;
        }

        /* extract info from filename */
        if ((istat = ExtractFilenameInfo(fn_loc_obs[nObsFile], ftype_obs))
                < 0)
            nll_puterr("WARNING: error extracting information from filename.");


        /* read arrivals */

        if (NumArrivals != OBS_FILE_SKIP_INPUT_LINE) {
            nll_putmsg(2, "");
            sprintf(MsgStr,
                    "Reading next set of observations (Files open: Tot:%d Buf:%d Hdr:%d  Alloc: %d) ...",
                    NumFilesOpen, NumGridBufFilesOpen, NumGridHdrFilesOpen, NumAllocations);
            nll_putmsg(1, MsgStr);
        }


        /* read next set of observations */

        numArrivalsNew = GetObservations(fp_obs,
                ftype_obs, fn_loc_grids, Arrival,
                &i_end_of_input, &numArrivalsIgnoreNew,
                &numArrivalsRejectNew,
                MaxNumArrLoc - NumArrivals, &Hypocenter,
                &maxArrExceeded, &numSArrivalsLocNew, NumArrivals);

        if (numArrivalsNew < 0)
            goto cleanup;

        NumArrivals += numArrivalsNew;
        numArrivalsIgnore += numArrivalsIgnoreNew;
        numArrivalsReject += numArrivalsRejectNew;
        numSArrivalsLocation += numSArrivalsLocNew;

        nll_putmsg(2, "");
        sprintf(MsgStr, "...end of observation file detected.");
        nll_putmsg(1, MsgStr);
        fclose(fp_obs);
        NumFilesOpen--;


    }

    iLocated = 0;

    /* set number of arrivals to be used in location */

    //DD
    //NumArrivalsLocation = NumArrivals - numArrivalsIgnore;
    NumArrivalsLocation = NumArrivals;

    nll_putmsg(2, "");
    sprintf(MsgStr,
            "... %d observations read, %d will be used for location (%s).",
            NumArrivals + numArrivalsReject, NumArrivalsLocation - numArrivalsIgnore,
            fn_path_output);
    nll_putmsg(1, MsgStr);


    /* sort to get rejected arrivals at end of arrivals array */
    /*DD
                    if ((istat = SortArrivalsIgnore(Arrival, NumArrivals + numArrivalsReject)) < 0) {
                            nll_puterr("ERROR: sorting arrivals by ignore flag.");
                                    goto cleanup;
                    }
     */

    /* check for minimum number of arrivals */

    if (NumArrivalsLocation < MinNumArrLoc) {
        sprintf(MsgStr,
                "WARNING: too few observations to locate (%d available, %d needed), skipping event.", NumArrivalsLocation, MinNumArrLoc);
        nll_putmsg(1, MsgStr);
        sprintf(MsgStr,
                "INFO: %d observations needed (specified in control file entry LOCMETH).",
                MinNumArrLoc);
        nll_putmsg(2, MsgStr);
        goto cleanup;
    }


    /* check for minimum number of S arrivals */

    if (numSArrivalsLocation < MinNumSArrLoc) {
        sprintf(MsgStr,
                "WARNING: too few S observations to locate (%d available, %d needed), skipping event.", numSArrivalsLocation, MinNumSArrLoc);
        nll_putmsg(1, MsgStr);
        sprintf(MsgStr,
                "INFO: %d S observations needed (specified in control file entry LOCMETH).",
                MinNumSArrLoc);
        nll_putmsg(2, MsgStr);
        goto cleanup;
    }


    /* process arrivals */

    /* add stations to station list */

    // AJL 20060105 removed
    //if (iSaveNLLocSum)
    //	NumStations += addToStationList(StationList, NumStations, Arrival, NumArrivals + numArrivalsReject);
    // end AJL 20060105 removed

    /* sort to get location arrivals in time order */
    /*DD
                    if ((istat =
                            SortArrivalsIgnore(Arrival, NumArrivals)) < 0) {
                                    nll_puterr("ERROR: sorting arrivals by ignore flag.");
                            goto cleanup;
                    }
     */
    /*DD
                    if ((istat =
                            SortArrivalsTime(Arrival, NumArrivalsLocation)) < 0) {
                                    nll_puterr("ERROR: sorting arrivals by time.");
                            goto cleanup;
                    }
     */

    /* construct weight matrix (TV82, eq. 10-9; MEN92, eq. 12) */
    /*DD
                    if ((istat = ConstWeightMatrix(NumArrivalsLocation, Arrival,
                                    &Gauss)) < 0) {
                            nll_puterr("ERROR: constructing weight matrix - NLLoc requires non-zero observation or modelisation errors.");
                            // close time grid files and continue
                            goto cleanup;
                    }
     */

    /* calculate weighted mean of obs arrival times   */
    /*	(TV82, eq. A-38) */
    /*DD
                    CalcCenteredTimesObs(NumArrivalsLocation, Arrival, &Gauss,
                                                    &Hypocenter);
     */


    /* read hypocenters */
    if ((NumHypocenters = GetHypocenters(fn_hypocenters, ftype_hypocenters,
            DiffHypocenters, MAX_NUM_DIFF_HYPOCENTERS)) <= 0) {
        nll_puterr("ERROR: no hypocenters read.");
        //break;
        goto cleanup;
    }

    /* assign event indices */
    int numberP, numberS, numberOther;
    nIgnored = AssignEventIndexes(NumHypocenters, DiffHypocenters, NumArrivalsLocation, Arrival, &mean_abs, &numberP, &numberS, &numberOther);
    if (nIgnored == NumArrivalsLocation) {
        sprintf(MsgStr, "ERROR: all %d phase difference obs not assigned to events, Npha=%d, NphaLoc=%d, mean_abs_dd=%f, nP=%d, nS=%d, nOther=%d",
                nIgnored, NumArrivalsLocation, NumArrivalsLocation - nIgnored, mean_abs, numberP, numberS, numberOther);
        nll_putmsg(1, MsgStr);
        //break;
        goto cleanup;
    } else if (nIgnored > 0) {
        sprintf(MsgStr, "WARNING: %d phase difference obs not assigned to events, Npha=%d, NphaLoc=%d, mean_abs_dd=%f, nP=%d, nS=%d, nOther=%d",
                nIgnored, NumArrivalsLocation, NumArrivalsLocation - nIgnored, mean_abs, numberP, numberS, numberOther);
        nll_putmsg(1, MsgStr);
    } else {
        sprintf(MsgStr, "%d phase differences not assigned to events, Npha=%d, NphaLoc=%d, mean_abs_dd=%f, nP=%d, nS=%d, nOther=%d",
                nIgnored, NumArrivalsLocation, NumArrivalsLocation - nIgnored, mean_abs, numberP, numberS, numberOther);
        nll_putmsg(1, MsgStr);
    }

    // save assigned obs to file
    sprintf(fname, "%s_ArrivalDiff_Init.cc", fn_path_output);
    if ((fp_out = fopen(fname, "w")) == NULL) {
        nll_puterr2("ERROR: opening HypoDDXCorrDiff phase file.", fname);
    } else {
        NumFilesOpen++;
        WriteHypoDDXCorrDiff(fp_out, NumArrivalsLocation, Arrival, DiffHypocenters);
        fclose(fp_out);
        NumFilesOpen--;
    }

    // count hypocenters for location
    numHypocentersAssigned = 0;
    for (nhyp = 0; nhyp < NumHypocenters; nhyp++) {
        if (!DiffHypocenters[nhyp].flag_ignore)
            numHypocentersAssigned++;
    }


    /* perform differential location */

    sprintf(MsgStr,
            "Locating... (Files open: Tot:%d Buf:%d Hdr:%d  Alloc: %d  3DMem: %d) ...",
            NumFilesOpen, NumGridBufFilesOpen, NumGridHdrFilesOpen, NumAllocations, Num3DGridReadToMemory);
    nll_putmsg(1, MsgStr);

    ngrid = 0;
    if ((NumLocationsCompleted = LocateDiff(fn_loc_obs[nObsFile], fn_path_output, numArrivalsReject)) < 0) {
        if (istat == GRID_NOT_INSIDE)
            //break;
            goto cleanup;
        else {
            nll_puterr("ERROR: location failed.");
            goto cleanup;
        }
    }

    iLocated = 1;

    // save final obs to files
    // NLL diff format
    sprintf(fname, "%s_ArrivalDiff_Final.nldiff", fn_path_output);
    if ((fp_out = fopen(fname, "w")) == NULL) {
        nll_puterr2("ERROR: opening HypoDDXCorrDiff nldiff phase file.", fname);
    } else {
        NumFilesOpen++;
        for (narr = 0; narr < NumArrivalsLocation; narr++)
            WriteDiffArrival(fp_out, DiffHypocenters, Arrival + narr, IO_ARRIVAL_ALL);
        fclose(fp_out);
        NumFilesOpen--;
    }
    // hypoDD cc format
    sprintf(fname, "%s_ArrivalDiff_Final.cc", fn_path_output);
    if ((fp_out = fopen(fname, "w")) == NULL) {
        nll_puterr2("ERROR: opening HypoDDXCorrDiff cc phase file.", fname);
    } else {
        NumFilesOpen++;
        WriteHypoDDXCorrDiff(fp_out, NumArrivalsLocation, Arrival, DiffHypocenters);
        fclose(fp_out);
        NumFilesOpen--;
    }

cleanup:
    ;


    /* release grid buffer or sheet storage */

    for (narr = 0; narr < NumArrivalsLocation; narr++) {
        //printf("DEBUG: narr %d %s  &(Arrival[narr].sheetdesc) %lx  &(.gdesc) %lx  .n_time_grid %d  .n_companion %d\n",
        //        narr, Arrival[narr].label, &(Arrival[narr].sheetdesc), &(Arrival[narr].gdesc), Arrival[narr].n_time_grid, Arrival[narr].n_companion);
        DestroyGridArray(&(Arrival[narr].sheetdesc));
        FreeGrid(&(Arrival[narr].sheetdesc));
        NLL_DestroyGridArray(&(Arrival[narr].gdesc));
        NLL_FreeGrid(&(Arrival[narr].gdesc));
    }
    NLL_FreeGridMemory();

    /* close time grid files (opened in function GetObservations) */

    for (narr = 0; narr < NumArrivalsLocation; narr++)
        CloseGrid3dFile(&(Arrival[narr].fpgrid), &(Arrival[narr].fphdr));

    if (iLocated) {
        nll_putmsg(2, "");
        sprintf(MsgStr,
                "Finished event location, output files: %s.* <%s.*.*.diff0.loc.hyp>",
                fn_path_output, fn_path_output);
        nll_putmsg(0, MsgStr);
    } else
        nll_putmsg(2, "");



    NumEvents = NumHypocenters;
    NumEventsLocated = numHypocentersAssigned;
    nll_putmsg(2, "");
    sprintf(MsgStr,
            "No more observation files.  %d events read,  %d events located,  %d locations completed.",
            NumEvents, NumEventsLocated, NumLocationsCompleted);
    nll_putmsg(0, MsgStr);
    nll_putmsg(2, "");


    /*
            / write cumulative arrival statistics
            for (ngrid = 0; ngrid < NumLocGrids; ngrid++)
                    if (LocGridSave[ngrid]) {
                            sprintf(fname, "%s.sum.grid%d.loc.stat", fn_path_output, ngrid);
                            if ((fpio = fopen(fname, "w")) == NULL) {
                                    nll_puterr2(
    "ERROR: opening cumulative phase statistics output file", fname);
                                    return(-1);
                            } else {
                                    NumFilesOpen++;
                            }
                            WriteStaStatTable(ngrid, fpio,
                                    RMS_Max, NRdgs_Min, Gap_Max,
                                    P_ResidualMax, S_ResidualMax, WRITE_RESIDUALS);
                            WriteStaStatTable(ngrid, fpio,
                                    RMS_Max, NRdgs_Min, Gap_Max,
                                    P_ResidualMax, S_ResidualMax, WRITE_RES_DELAYS);
                            WriteStaStatTable(ngrid, fpio,
                                    RMS_Max, NRdgs_Min, Gap_Max,
                                    P_ResidualMax, S_ResidualMax, WRITE_PDF_RESIDUALS);
                            WriteStaStatTable(ngrid, fpio,
                                    RMS_Max, NRdgs_Min, Gap_Max,
                                    P_ResidualMax, S_ResidualMax, WRITE_PDF_DELAYS);
                            fclose(fpio);
                            // save to last
                            sprintf(sys_command, "cp %s %slast.stat", fname, f_outpath);
                            system(sys_command);
                            // write delays only
                            sprintf(fname, "%s.sum.grid%d.loc.stat_totcorr", fn_path_output, ngrid);
                            if ((fpio = fopen(fname, "w")) == NULL) {
                                    nll_puterr2(
    "ERROR: opening total phase corrections output file", fname);
                                    return(-1);
                            } else {
                                    NumFilesOpen++;
                            }
                            WriteStaStatTable(ngrid, fpio,
                                    RMS_Max, NRdgs_Min, Gap_Max,
                                    P_ResidualMax, S_ResidualMax, WRITE_RES_DELAYS);
                            fclose(fpio);
                            // save to last
                            sprintf(sys_command, "cp %s %slast.stat_totcorr", fname, f_outpath);
                            system(sys_command);
                    }

            // write station list
            for (ngrid = 0; ngrid < NumLocGrids; ngrid++)
                    if (LocGridSave[ngrid]) {
                            sprintf(fname, "%s.sum.grid%d.loc.stations", fn_path_output, ngrid);
                            if ((fpio = fopen(fname, "w")) == NULL) {
                                    nll_puterr2(
    "ERROR: opening cumulative phase statistics output file", fname);
                                    return(-1);
                            } else {
                                    NumFilesOpen++;
                            }
                            WriteStationList(fpio, StationList, NumStations);
                            fclose(fpio);
                            // save to last
                            sprintf(sys_command, "cp %s %slast.stations", fname, f_outpath);
                            system(sys_command);
                    }
     */

    exit(EXIT_NORMAL);

}

/*** function to read input file */

int ReadNLDiffLoc_Input(FILE* fp_input) {
    int istat, iscan;
    char param[MAXLINE] = "\0";
    char line[MAXLINE_LONG], *fgets_return;

    int flag_hypofile = 0, flag_search = 0;
    int flag_include = 1;



    /* read each input line */

    /* read each input line */

    while ((fgets_return = fgets(line, 4 * MAXLINE, fp_input)) != NULL
            || fp_include != NULL) {


        /* check for end of include file */

        if (fgets_return == NULL && fp_include != NULL) {
            SwapBackIncludeFP(&fp_input);
            continue;
        }


        istat = -1;

        /*read parameter line */

        if ((iscan = sscanf(line, "%s", param)) < 0)
            continue;

        /* skip comment line or white space */

        if (strncmp(param, "#", 1) == 0 || isspace(param[0]))
            istat = 0;


        /* read include file params and set input to include file */

        if (strcmp(param, "INCLUDE") == 0)
            if ((istat = GetIncludeFile(strchr(line, ' '), &fp_input)) < 0) {
                nll_puterr("ERROR: processing include file.");
                flag_include = 0;
            }


        /* read hypocenter file name */

        if (strcmp(param, "DLOC_HYPFILE") == 0) {
            if ((istat = GetNLDiffLoc_HypFile(strchr(line, ' '))) < 0)
                nll_puterr("ERROR: reading NLDiffLoc hypocenter file name.");
            else
                flag_hypofile = 1;
        }


        /* read search type */

        if (strcmp(param, "DLOC_SEARCH") == 0) {
            if ((istat = GetNLDiffLoc_SearchType(strchr(line, ' '))) < 0)
                nll_puterr("ERROR: reading NLDiffLoc search type.");
            else
                flag_search = 1;
        }




    }


    /* check for missing required input */

    if (!flag_hypofile)
        nll_puterr("ERROR: reading i/o file (DLOC_HYPFILE) params.");
    if (!flag_search)
        nll_puterr("ERROR: reading i/o file (DLOC_SEARCH) params.");



    return (flag_hypofile && flag_search);

}

/*** function to read output file name ***/

int GetNLDiffLoc_HypFile(char* line1) {
    int istat;

    istat = sscanf(line1, "%s %s %d %d", fn_hypocenters, ftype_hypocenters, &NumHypocenterFix, &NumHypocenterFree);

    sprintf(MsgStr, "DLOC_HYPFILE:  File: %s  FileType: %s Fix: %d Free: %d",
            fn_hypocenters, ftype_hypocenters, NumHypocenterFix, NumHypocenterFree);
    nll_putmsg(2, MsgStr);

    if (istat != 4)
        return (-1);

    return (0);
}

/*** function to read search type ***/

int GetNLDiffLoc_SearchType(char* line1) {
    int istat, ierr;

    char search_type[MAXLINE];


    istat = sscanf(line1, "%s", search_type);

    if (istat != 1)
        return (-1);

    if (strcmp(search_type, "MET") == 0) {

        SearchType = SEARCH_MET;
        istat = sscanf(line1, "%s %d %d %d %lf %lf %lf %lf",
                search_type, &MetNumSamples, &MetStartSave, &MetSkip, &MetStepInit, &MetVelocity, &MetInititalTemperature, &MetStepMax);
        ierr = 0;

        sprintf(MsgStr,
                "DLOC_SEARCH:  Type: %s  numSamples %d  startSave %d  numSkip %d  step %lf  velocity %lf  init_temp %lf max_step %lf",
                search_type, MetNumSamples, MetStartSave, MetSkip, MetStepInit, MetVelocity, MetInititalTemperature, MetStepMax);
        nll_putmsg(2, MsgStr);

        if (checkRangeInt("DLOC_SEARCH", "numSamples", MetNumSamples, 1, 0, 0, 0) != 0)
            ierr = -1;
        if (checkRangeInt("DLOC_SEARCH", "startSave", MetStartSave, 1, 0, 0, 0) != 0)
            ierr = -1;
        if (checkRangeInt("DLOC_SEARCH", "numSkip", MetSkip, 1, 1, 0, 0) != 0)
            ierr = -1;
        if (checkRangeDouble("DLOC_SEARCH", "step_init", MetStepInit, 1, 0.0, 0, 0.0) != 0)
            ierr = -1;
        if (checkRangeDouble("DLOC_SEARCH", "velocity", MetVelocity, 1, -1000.0, 1, 1000.0) != 0)
            ierr = -1;
        if (checkRangeDouble("DLOC_SEARCH", "init_temp", MetInititalTemperature, 1, 0.0, 0, 0.0) != 0)
            ierr = -1;
        if (checkRangeDouble("DLOC_SEARCH", "step_max", MetStepMax, 1, 0.0, 0, 0.0) != 0)
            ierr = -1;
        if (ierr < 0)
            return (-1);

        if (istat < 7)
            return (-1);
        if (istat == 7) // MetStepMax not initialized
            MetStepMax = DBL_MAX;

        //?? AJL 17JAN2000 MetUse = MetNumSamples - MetEquil;
        MetUse = MetNumSamples - MetStartSave;

    }


    return (0);
}

/*** function to read hypocenters file */

int GetHypocenters(char* fn_hypos, char* ftype_hypos, HypoDesc* Hypos, int max_num_hypos) {

    int i, istat;

    int nHypos = 0;
    FILE* pHypoFile;


    if ((pHypoFile = fopen(fn_hypos, "r")) == NULL) {
        nll_puterr2("ERROR: opening hypocenters", fn_hypos);
        return (-1);
    } else {
        NumFilesOpen++;
    }

    if (strcmp(ftype_hypos, "NLLOC_SUM") == 0) {

        while (nHypos < max_num_hypos) {
            istat = GetHypLoc(pHypoFile, fn_hypos, &Hypos[nHypos],
                    NULL, NULL, 0, NULL, -1);
            if (istat < 0)
                break;
            // initialize some hypo fields
            latlon2rect(0, Hypos[nHypos].dlat, Hypos[nHypos].dlong, &(Hypos[nHypos].x), &(Hypos[nHypos].y));
            Hypos[nHypos].z = Hypos[nHypos].depth;
            Hypos[nHypos].dotime = 0.0;

            Hypos[nHypos].flag_ignore = 1;
            // increment index
            nHypos++;
        }
        if (nHypos >= max_num_hypos) {
            sprintf(MsgStr, "WARNING: maximum number of hypocenters exceeded: %d", max_num_hypos);
            nll_putmsg(1, MsgStr);
        }

    } else if (strcmp(ftype_hypos, "HYPODD_INIT") == 0) {

        while (nHypos < max_num_hypos) {
            istat = ReadHypoDDInitHypo(pHypoFile, &Hypos[nHypos], 0);
            if (istat < 0)
                break;
            // initialize some hypo fields
            Hypos[nHypos].flag_ignore = 1;
            // increment index
            nHypos++;
        }
        if (nHypos >= max_num_hypos) {
            sprintf(MsgStr, "WARNING: maximum number of hypocenters exceeded: %d", max_num_hypos);
            nll_putmsg(1, MsgStr);
        }

    } else {

        fclose(pHypoFile);
        NumFilesOpen--;
        nll_puterr2("ERROR: unrecognized hypocenters format: ", ftype_hypos);
        return (-1);

    }

    fclose(pHypoFile);
    NumFilesOpen--;

    /* display hypocenter to std out */
    sprintf(MsgStr, "Read %d initial hypocenters.", nHypos);
    nll_putmsg(1, MsgStr);
    if (message_flag > 1) {
        for (i = 0; i < nHypos; i++)
            WriteHypoDDInitHypo(stdout, &Hypos[i]);
    }

    return (nHypos);

}

/*** function to perform grid search location */

int LocateDiff(char* fn_obs, char* fn_path_output, int numArrivalsReject) {

    int istat, n;
    int nLocationsCompleted;

    FILE *fpio;
    char fname[FILENAME_MAX];
    char fn_root_out[FILENAME_MAX];
    int nSamplesTotal = -1;
    float *fdata, ftemp;
    long int iFdataOffset, iSizeOfFdata;
    double alpha_2;

    HypoDesc *phypo;




    /* write message */

    nll_putmsg(2, "");
    sprintf(MsgStr, "Applying Metropolis within Grid:");
    nll_putmsg(2, MsgStr);
    if (message_flag >= 3)
        display_grid_param(LocGrid);

    /* allocate scatter array for saved samples */
    iFdataOffset = (1 + MetUse / MetSkip) * 4;
    iSizeOfFdata = NumHypocenters * iFdataOffset * sizeof (float);
    if ((fdata = (float *) malloc(iSizeOfFdata)) == NULL) {
        nll_puterr("ERROR: allocating scatter sample array.");
        return (EXIT_ERROR_LOCATE);
    }
    NumAllocations++;


    /* open summary output file for initial hypocenters */

    if ((istat = OpenSummaryFiles(fn_path_output, "diff_init")) < 0) {
        nll_puterr("FATAL ERROR: opening hypocenter summary files.");
        exit(EXIT_ERROR_FILEIO);
    }

    /* initialize hypocenters */
    for (n = 0; n < NumHypocenters; n++) {
        phypo = DiffHypocenters + n;
        /* initialize hypocenter fields */
        sprintf(phypo->locStat, "INIT");
        sprintf(phypo->locStatComm, "Not located.");
        strcpy(phypo->comment, Hypocenter.comment);
        sprintf(phypo->searchInfo,
                "METROPOLIS nSamp 0 nAcc 0 nSave 0 nClip 0 Dstep0 0 Dstep 0");
        /* set time (differential time from otime */
        phypo->time = 0.0L;
        phypo->nSamples = 0;
        phypo->ipos = n * iFdataOffset;
        phypo->nScatterSaved = 0;
        /* set output name */
        sprintf(fn_root_out, "%s.%4.4d%2.2d%2.2d.%2.2d%2.2d%2.2d",
                fn_path_output,
                phypo->year, phypo->month, phypo->day,
                phypo->hour, phypo->min, (int) phypo->sec);
        // calculate initial location quality
        DiffLocSaveBestLocation(NumArrivals, NumArrivalsLocation, Arrival, LocGrid,
                NULL, n, LocGrid[0].type);
        // save initial location
        sprintf(fname, "%s.diff_init0", fn_root_out);
        strcpy(phypo->fileroot, fname);
        if ((istat = WriteGrid3dHdr(LocGrid, NULL, phypo->fileroot, "loc")) < 0) {
            return (istat);
        }
        if ((istat = SaveLocation(&(DiffHypocenters[n]), 0, fn_obs, fname, numArrivalsReject, "diff_init", 1, NULL)) < 0)
            return (istat);
        // set full outname
        sprintf(fname, "%s.diff0", fn_root_out);
        strcpy(phypo->fileroot, fname);
        sprintf(phypo->locStat, "LOCATED");
        sprintf(phypo->locStatComm, "Location completed.");
    }


    CloseSummaryFiles();



    /* open summary output file for location */

    if ((istat = OpenSummaryFiles(fn_path_output, "diff")) < 0) {
        nll_puterr("FATAL ERROR: opening hypocenter summary files.");
        exit(EXIT_ERROR_FILEIO);
    }



    /* search type dependent initializations */


    /* initial step size */
    Metrop.dx = MetStepInit;
    /* set likelihood */
    Metrop.likelihood = -1.0;
    Metrop.velocity = MetVelocity;
    Metrop.initial_temperature = MetInititalTemperature;


    /* since sorted, reset companion indices */
    /*DD
            if (VpVsRatio > 0.0) {
                    //for (narr = 0; narr < NumArrivalsLocation; narr++) {
                    for (narr = 0; narr < NumArrivalsLocation; narr++) {
                            if (Arrival[narr].n_companion < 0)
                                    continue;
                            if (IsPhaseID(Arrival[narr].phase, "S") &&
                                            (Arrival[narr].n_companion =
                                            IsDuplicateArrival(Arrival, narr, narr, "P")) < 0) {
                                    nll_puterr("ERROR: cannot find companion arrival.");
                                    return(EXIT_ERROR_LOCATE);
                            }
                    }
            }
     */

    /* do search */

    /* Metropolis location (random walk) */
    if ((nSamplesTotal =
            DiffLocMetropolis(NumArrivals, NumArrivalsLocation,
            Arrival, LocGrid,
            &Gauss, &Metrop, fdata)) < 0) {
        nll_puterr("ERROR: in Metropolis location.");
        return (EXIT_ERROR_LOCATE);
    }



    /*DD
            // clean up dates
            StdDateTime(Arrival, NumArrivals, &Hypocenter);

            // determine azimuth gap
            Hypocenter.gap = CalcAzimuthGap(Arrival, NumArrivalsLocation);
     */

    /* re-sort arrivals by distance */

    //	if ((istat = SortArrivalsDist(Arrival, NumArrivals)) < 0) {
    //		nll_puterr("ERROR: sorting arrivals by distance.");
    //		return(EXIT_ERROR_LOCATE);
    //	}


    /*DD
            // save distance to closest station
            Hypocenter.dist = Arrival->dist;
     */


    // search type dependent processing

    alpha_2 = 3.53; // value for 68% conf (see Num Rec, 2nd ed, sec 15.6)

    nLocationsCompleted = 0;

    for (n = 0; n < NumHypocenters; n++) {

        phypo = DiffHypocenters + n;

        if (phypo->flag_ignore)
            continue;

        nLocationsCompleted++;

        /* re-calculate solution and arrival statistics for best location */

        DiffLocSaveBestLocation(NumArrivals, NumArrivalsLocation, Arrival, LocGrid, NULL, n, LocGrid[0].type);

        if (!phypo->flag_ignore) {
            printf("> nHypo %d (%4.4d%2.2d%2.2d %2.2d%2.2d%2.2d x%f y%f z%f)  mf_min %f like_max %e  %s",
                    n, phypo->year, phypo->month, phypo->day,
                    phypo->hour, phypo->min, (int) phypo->sec,
                    phypo->x, phypo->y, phypo->z,
                    phypo->misfit, (double) phypo->probmax, phypo->locStat);
            if (NumHypocenterFix >= 0 && n == NumHypocenterFix) {
                printf("  FIXED\n");
            } else if (NumHypocenterFree >= 0 && n != NumHypocenterFree) {
                printf("  FIXED\n");
            } else {
                printf("\n");
            }
        }
        // write scatter file
        sprintf(fname, "%s.loc.scat", phypo->fileroot);
        if ((fpio = fopen(fname, "w")) != NULL) {
            // write scatter file header informaion
            fseek(fpio, 0, SEEK_SET);
            fwrite(&(phypo->nScatterSaved), sizeof (int), 1, fpio);
            ftemp = (float) phypo->probmax;
            fwrite(&ftemp, sizeof (float), 1, fpio);
            // skip header record
            fseek(fpio, 4 * sizeof (float), SEEK_SET);
            // write scatter samples
            fwrite(fdata + n * iFdataOffset, 4 * sizeof (float), phypo->nScatterSaved, fpio);
            fclose(fpio);
        } else {
            nll_puterr("ERROR: opening scatter output file.");
            return (EXIT_ERROR_IO);
        }

        // calculate "traditional" statistics
        phypo->expect =
                CalcExpectationSamples(fdata + n * iFdataOffset, phypo->nScatterSaved);
        istat = rect2latlon(0, phypo->expect.x, phypo->expect.y,
                &(phypo->expect_dlat), &(phypo->expect_dlong));
        phypo->cov = CalcCovarianceSamples(fdata + n * iFdataOffset, phypo->nScatterSaved,
                &phypo->expect);
        if (phypo->nScatterSaved) {
            phypo->ellipsoid = CalcErrorEllipsoid(
                    &phypo->cov, alpha_2);
        }


        // search type dependent results saving


        // save location grid header to disk

        if (LocGridSave[0])
            if ((istat = WriteGrid3dHdr(LocGrid, NULL, phypo->fileroot, "loc")) < 0) {
                nll_puterr("ERROR: writing grid header to disk.");
                return (EXIT_ERROR_IO);
            }



        // display and save minimum misfit location to file

        if (LocGridSave[0]) {

            /*DD
                                    // calculate magnitudes
                                    phypo->amp_mag = MAGNITUDE_NULL;
                                    phypo->num_amp_mag = 0;
                                    phypo->dur_mag = MAGNITUDE_NULL;
                                    phypo->num_dur_mag = 0;
                                    for (n = 0; n < MAX_NUM_MAG_METHODS; n++)
                                            CalculateMagnitude(&DiffHypocenters[n], Arrival, NumArrivals,
                                                    Component, NumCompDesc, Magnitude + n);
                                    // calculate estimated VpVs ratio
                                    CalculateVpVsEstimate(&DiffHypocenters[n], Arrival, NumArrivals);
             */
            // save location
            if ((istat = SaveLocation(&(DiffHypocenters[n]), 0, fn_obs,
                    phypo->fileroot, 0, "diff", 1, NULL)) < 0) {
                nll_puterr2("ERROR: saving location.", phypo->fileroot);
                //return(istat);
            }
            /*DD
                                    // update station statistics table
                                    if (strncmp(phypo->locStat, "LOCATED", 7) == 0
                                                    && phypo->rms <= RMS_Max
                                                    && phypo->nreadings >= NRdgs_Min
                                                    && phypo->gap <= Gap_Max)
                                            UpdateStaStat(0, Arrival, NumArrivals,
                                                    P_ResidualMax, S_ResidualMax);
             */

        }


    }

    CloseSummaryFiles();


    // write differential time event links to file
    sprintf(fname, "%s_DiffTimeLinks.xyz", fn_path_output);
    FILE *fp_out;
    if ((fp_out = fopen(fname, "w")) == NULL) {
        nll_puterr2("ERROR: opening HypoDDXCorrDiff nldiff phase file.", fname);
    } else {
        NumFilesOpen++;
        SaveDiffTimeLinks(NumHypocenters, DiffHypocenters, NumArrivals, Arrival, fp_out);
        fclose(fp_out);
        NumFilesOpen--;
    }

    // write hypoDD residuals output to file
    sprintf(fname, "%s.res", fn_path_output);
    if ((fp_out = fopen(fname, "w")) == NULL) {
        nll_puterr2("ERROR: opening hypoDD format Data residual output file.", fname);
    } else {
        NumFilesOpen++;
        SaveHypoDDRes(NumHypocenters, DiffHypocenters, NumArrivals, Arrival, fp_out);
        fclose(fp_out);
        NumFilesOpen--;
    }


    // search type dependent cleanup


    // free saved samples memory
    free(fdata);
    NumAllocations--;



    /* re-sort to get location arrivals in time order */
    /*DD
            if ((istat =
                    SortArrivalsIgnore(Arrival, NumArrivals)) < 0) {
                            nll_puterr(
                            "ERROR: sorting arrivals by ignore flag.");
                    return(EXIT_ERROR_LOCATE);
            }
            if ((istat = SortArrivalsTime(Arrival, NumArrivalsLocation)) < 0) {
                    nll_puterr("ERROR: sorting arrivals by time.");
                    return(EXIT_ERROR_LOCATE);
            }
     */



    /* search type dependent return */

    if (nSamplesTotal < 1) {
        nll_puterr("ERROR: no Metropolis sameples saved.");
        return (-1);
    }


    return (nLocationsCompleted);



}







/*** function to perform Metropolis location */

#define HUGE_MISFIT 1.0e30
#define SMALLEST_LIKELIHOOD 1.0e-300
#define TARGET_NUM_MET_TRIES 4
#define MAX_NUM_MET_TRIES (2*TARGET_NUM_MET_TRIES-1);

int DiffLocMetropolis(int num_arr_total, int num_arr_loc,
        ArrivalDesc *arrival,
        GridDesc* ptgrid, GaussLocParams* gauss_par,
        WalkParams* pMetrop, float* fdata) {

    int istat, itest;
    int ntryTotal, nSamplesTotal, nAcceptMax, nAcceptMin;
    long int nGenerated;
    int maxNumTries = MAX_NUM_MET_TRIES;
    int iFinishedSome = 0;
    int writeMessage = 0;
    int iGridType;
    int nReject, numClipped = 0, numGridReject = 0;
    int iBoundary = 0;
    int iAccept = 0;
    double xval, yval, zval, tval;
    double currentMetStepFact;

    double value, dlike;

    double misfit;

    double xmin, xmax, ymin, ymax, zmin, zmax;
    double dx_init = 1.0;

    if (NumHypocenterFree >= 0)
        iFinishedSome = 1;

    // DD
    HypoDesc* phypo;
    int nHypo;
    //	char locStatComm[2 * MAXLINE];
    Vect3D hypo_test;
    int imessage_modulo;
    // working arrays
    double hyp_value[NumHypocenters];
    double hyp_dlike[NumHypocenters];
    double hyp_dlike_sum[NumHypocenters];
    int hyp_dlike_num_mean[NumHypocenters];
    Vect3D hyp_xyz[NumHypocenters];
    double hyp_dt[NumHypocenters];
    int hyp_fixed[NumHypocenters];
    int hyp_abort[NumHypocenters];
    int ntry[NumHypocenters];
    double metrop_dx[NumHypocenters];

    char status_msg[MAXLINE_LONG] = "";
    int nLowAcc, iHypo;
    double like_init_best = -1.0;
    int iHypo_like_init_best = -1;
    int nHypoActive, nHypoActiveInit;



    // get solution quality at each sample on random walk

    nll_putmsg(3, "");
    nll_putmsg(3, "Calculating solution along Metropolis walk...");

    iGridType = ptgrid[0].type;

    // 20110725 AJL - added temperature
    // temperature allows Metropolis dx and effective LOCQUAL2ERR diff time error to be larger for early samples
    // Metropolis dx and time error is multiplied by temperature
    double temperature = 1.0;

    // initialize hypocenter working arrays
    nHypoActive = 0;
    for (nHypo = 0; nHypo < NumHypocenters; nHypo++) {
        phypo = DiffHypocenters + nHypo;
        if (phypo->flag_ignore)
            continue;
        hyp_xyz[nHypo].x = phypo->x;
        hyp_xyz[nHypo].y = phypo->y;
        hyp_xyz[nHypo].z = phypo->z;
        hyp_dt[nHypo] = phypo->dotime;
        // init likelihood
        value = DiffLocCalcSolutionQuality(hyp_xyz[nHypo], hyp_dt[nHypo],
                nHypo, NumHypocenters, DiffHypocenters,
                num_arr_loc, arrival, gauss_par,
                iGridType, temperature, &misfit, NULL, &nReject, 0);
        if (nReject) {
            phypo->misfit = HUGE_MISFIT;
            phypo->probmax = 0.0;
        } else {
            phypo->misfit = misfit;
            phypo->probmax = exp(value);
        }
        hyp_value[nHypo] = value;
        hyp_dlike[nHypo] = phypo->probmax;
        hyp_dlike_sum[nHypo] = phypo->probmax;
        hyp_dlike_num_mean[nHypo] = 1;
        phypo->grid_misfit_max = phypo->misfit;
        metrop_dx[nHypo] = dx_init = 2.0 * pMetrop->dx;
        ntry[nHypo] = TARGET_NUM_MET_TRIES;
        printf("< nHypo %d (%4.4d%2.2d%2.2d %2.2d%2.2d%2.2d x%f y%f z%f)  mf_init %f like_init %e step %g",
                nHypo, phypo->year, phypo->month, phypo->day,
                phypo->hour, phypo->min, (int) phypo->sec,
                phypo->x, phypo->y, phypo->z,
                phypo->misfit, (double) phypo->probmax, metrop_dx[nHypo]);
        hyp_fixed[nHypo] = 0;
        if (NumHypocenterFix >= 0 && nHypo == NumHypocenterFix) {
            hyp_fixed[nHypo] = 1;
        } else if (NumHypocenterFree >= 0 && nHypo != NumHypocenterFree) {
            hyp_fixed[nHypo] = 1;
        }
        if (hyp_fixed[nHypo])
            printf("  FIXED\n");
        else
            printf("\n");
        hyp_abort[nHypo] = 0;
        // check for best
        if (phypo->probmax > like_init_best) {
            like_init_best = phypo->probmax;
            iHypo_like_init_best = nHypo;
        }
        // count active
        if (!hyp_fixed[nHypo] && !phypo->flag_ignore)
            nHypoActive++;
    }
    nHypoActiveInit = nHypoActive;
    printf("<<  like_init_best %e  nHypo %d\n", like_init_best, iHypo_like_init_best);

    // set walk limits equal to grid limits
    xmin = ptgrid->origx;
    xmax = xmin + (double) (ptgrid->numx - 1) * ptgrid->dx;
    ymin = ptgrid->origy;
    ymax = ymin + (double) (ptgrid->numy - 1) * ptgrid->dy;
    zmin = ptgrid->origz;
    zmax = zmin + (double) (ptgrid->numz - 1) * ptgrid->dz;

    // save initial values
    currentMetStepFact = MetStepFact;


    // loop over walk samples

    ntryTotal = 0;
    nSamplesTotal = 0;
    nHypo = 0;
    nGenerated = 0;
    nAcceptMax = 0;
    nAcceptMin = 0;
    imessage_modulo = (200001 * NumHypocenters / num_arr_loc);

    while (1) {

        // set next hypocenter
        itest = 0;
        while (itest < NumHypocenters) {
            nHypo++;
            if (nHypo >= NumHypocenters) {
                nHypo = 0;
                nAcceptMin = nAcceptMax; // nAcceptMin will be approximate
            }
            phypo = DiffHypocenters + nHypo;
            if (!hyp_fixed[nHypo] && !phypo->flag_ignore && !hyp_abort[nHypo] && phypo->nSamples < MetNumSamples) {
                if (phypo->nSamples < nAcceptMin)
                    nAcceptMin = phypo->nSamples;
                break;
            }
            itest++;
            //			if (phypo->nSamples >= MetNumSamples)
            //				iFinishedSome = 1;
        }
        // check if all non-ignored hypos have reached MetNumSamples
        if (itest >= NumHypocenters)
            break;
        //printf("TP 1 - nHypo %d\n", nHypo);

        // set met params
        pMetrop->x = hyp_xyz[nHypo].x;
        pMetrop->y = hyp_xyz[nHypo].y;
        pMetrop->z = hyp_xyz[nHypo].z;
        pMetrop->dt = hyp_dt[nHypo];

        // adjust met step as a function of last ntry for this hypo
        if (ntry[nHypo] <= TARGET_NUM_MET_TRIES && metrop_dx[nHypo] < MetStepMax) {
            metrop_dx[nHypo] *= 1.01; // increase step slowly
            //printf("DEBUG: INCREASE: metrop_dx[nHypo] %f\n", metrop_dx[nHypo]);
        } else if (ntry[nHypo] > TARGET_NUM_MET_TRIES) {
            //metrop_dx[nHypo] = pMetrop->dx; 	// decrease step immediately
            metrop_dx[nHypo] /= 1.1; // decrease step quickly
            if (metrop_dx[nHypo] < pMetrop->dx)
                metrop_dx[nHypo] = pMetrop->dx;
            //printf("DEBUG: DECREASE: metrop_dx[nHypo] %f\n", metrop_dx[nHypo]);
        }

        ntry[nHypo] = 0;
        while (1) {

            // check abort search conditions

            // failure to accept sample after maxNumTries
            if (ntry[nHypo] >= maxNumTries) {
                if (iFinishedSome) {
                    if (phypo->nSamples < (MetNumSamples * 9 / 10)) {
                        if (message_flag > 0)
                            fprintf(stdout, "\n");
                        sprintf(MsgStr,
                                "WARNING: acceptance rate low %d/%d, stopping search for hypocenter %d.", phypo->nSamples, nAcceptMax, nHypo);
                        nll_puterr(MsgStr);
                        hyp_abort[nHypo] = 1;
                        nHypoActive--;
                        sprintf(phypo->locStat, "ABORTED");
                        sprintf(phypo->locStatComm, "%s", MsgStr);
                    } else if (phypo->probmax < SMALLEST_LIKELIHOOD) {
                        if (message_flag > 0)
                            fprintf(stdout, "\n");
                        sprintf(MsgStr,
                                "WARNING: likeAve too low %.2e, stopping search for hypocenter %d.", (double) phypo->probmax, nHypo);
                        nll_puterr(MsgStr);
                        hyp_abort[nHypo] = 1;
                        sprintf(phypo->locStat, "ABORTED");
                        sprintf(phypo->locStatComm, "%s", MsgStr);
                    }
                }
                break;
            }

            // increment sample

            ntry[nHypo]++;
            nGenerated++;

            if (message_flag > 0 && nGenerated % imessage_modulo == 0/* || ntry >= maxNumTries*/) {
                // check how many hypos have low rate of acceptance
                nLowAcc = 0;
                for (iHypo = 0; iHypo < NumHypocenters; iHypo++) {
                    if (ntry[iHypo] >= maxNumTries || (DiffHypocenters + iHypo)->nSamples < nAcceptMax / 10)
                        nLowAcc++;
                }
                sprintf(status_msg,
                        "nAct %d nLAcc %d  Acc %d/%d (%d-%d) TryAve %.1f (hyp %d dx %.3g Try %d Acc %d/%d mfMin %.3f like:Max %.2e Ave %.2e)\r",
                        nHypoActive, nLowAcc,
                        nSamplesTotal, (int) nGenerated, nAcceptMin, nAcceptMax,
                        (double) ntryTotal / (double) nSamplesTotal,
                        nHypo, metrop_dx[nHypo], ntry[nHypo], phypo->nSamples, MetNumSamples,
                        phypo->misfit, (double) phypo->probmax,
                        hyp_dlike_sum[nHypo] / (double) hyp_dlike_num_mean[nHypo]
                        );
                fprintf(stdout, "%s", status_msg);
                fflush(stdout);
            }

            // set variable temperature
            if (phypo->nSamples < MetStartSave)
                temperature = 1.0 + (pMetrop->initial_temperature - 1.0) * (1.0 - (double) phypo->nSamples / (double) MetStartSave);
            else
                temperature = 1.0;


            istat = DiffLocGetNextMetropolisSample(pMetrop, temperature * metrop_dx[nHypo],
                    xmin, xmax, ymin, ymax,
                    zmin, zmax, &xval, &yval, &zval, &tval);
            if (istat > 0) {
                phypo->numClipped += istat;
                numClipped += istat;
                continue;
            }
            //printf("TP 2 - istat %d\n", istat);

            // re-calculate misfit for current sample, since other events may have moved
            /* seems to make little difference in hypoDD_example1 test,  but takes 2x time
            hyp_value[nHypo] = DiffLocCalcSolutionQuality(hyp_xyz[nHypo], hyp_dt[nHypo],
                            nHypo, NumHypocenters, DiffHypocenters,
                            num_arr_loc, arrival, gauss_par,
                            iGridType, temperature, &misfit, NULL, &nReject, 0);
            hyp_dlike[nHypo] = exp(hyp_value[nHypo]);
             */

            // calc new misfit or prob density
            hypo_test.x = xval;
            hypo_test.y = yval;
            hypo_test.z = zval;
            value = DiffLocCalcSolutionQuality(hypo_test, tval,
                    nHypo, NumHypocenters, DiffHypocenters,
                    num_arr_loc, arrival, gauss_par,
                    iGridType, temperature, &misfit, NULL, &nReject, 0);
            //if (fabs(value) > 1.0)
            //printf("TP 3 - nHypo  %d value %lf\n", nHypo, value);

            if (nReject) {

                numGridReject++;
                misfit = HUGE_MISFIT;
                dlike = 0.0;
                continue;

            } else {

                dlike = exp(value);

                /* apply Metropolis test */
                //iAccept = MetropolisTest(hyp_dlike[nHypo], dlike);
                iAccept = DiffLocMetropolisTest(hyp_value[nHypo], value, hyp_dlike[nHypo], dlike);

                /* if not accepted, but at maxNumTries... */
                /*DD
                                                if (!iAccept && ntry[nHypo] == maxNumTries) {
                                                        // try accept anyway since may be stuck in a deep minima
                                                        if (numAcceptDeepMinima++ < 5) {
                                                                iAccept = 1;
                        printf("Max Num Tries: accept deep minima\n");
                                                                // try reducing step size
                                                                currentMetStepFact /= 2.0;
                                                                ntry[nHypo] = 0;
                        printf("            +: step ch: was %lf\n", pMetrop->dx);
                                                        }
                                                }
                 */

                if (iAccept) {

                    nSamplesTotal++;
                    if (nSamplesTotal % 1000 == 1)
                        writeMessage = 1;

                    (phypo->nSamples)++;
                    if (phypo->nSamples == MetNumSamples) {
                        iFinishedSome = 1;
                        nHypoActive--;
                    }
                    if (phypo->nSamples > nAcceptMax)
                        nAcceptMax = phypo->nSamples;

                    /* check for minimum misfit */
                    if (misfit <= phypo->misfit) {
                        phypo->probmax = dlike;
                        phypo->misfit = misfit;
                        phypo->x = xval;
                        phypo->y = yval;
                        phypo->z = zval;
                        phypo->dotime = tval;
                        /*
                                                                for (narr = 0; narr < num_arr_loc; narr++)
                                                                        arrival[narr].pred_travel_time_best =
                                                                                arrival[narr].pred_travel_time;
                         */
                    }
                    if (misfit > phypo->grid_misfit_max)
                        phypo->grid_misfit_max = misfit;

                    /* update sample location */
                    hyp_xyz[nHypo].x = xval;
                    hyp_xyz[nHypo].y = yval;
                    hyp_xyz[nHypo].z = zval;
                    hyp_dt[nHypo] = tval;
                    hyp_value[nHypo] = value;
                    hyp_dlike[nHypo] = dlike;
                    hyp_dlike_sum[nHypo] += dlike;
                    hyp_dlike_num_mean[nHypo]++;

                    /* if saving samples */
                    if (phypo->nSamples > MetStartSave && phypo->nSamples % MetSkip == 0) {

                        /* save sample to scatter file */
                        fdata[(phypo->ipos)] = xval;
                        fdata[++(phypo->ipos)] = yval;
                        fdata[++(phypo->ipos)] = zval;
                        fdata[++(phypo->ipos)] = dlike;
                        ++(phypo->ipos);
                        ++(phypo->nScatterSaved);
                    }

                    break;

                }

                /*
                if (message_flag >= 3 && writeMessage || ntry[nHypo] == maxNumTries - 1) {
                                sprintf(MsgStr,
                "Metropolis: n %d x %.2lf y %.2lf z %.2lf  dx %.2lf  li %.2le",
                nSamplesTotal, pMetrop->x, pMetrop->y, pMetrop->z, pMetrop->dx, pMetrop->likelihood);
                                nll_putmsg(3, MsgStr);
                                writeMessage = 0;
                }
                 */

            }

        }
        ntryTotal += ntry[nHypo];

    }




    if (message_flag > 0)
        fprintf(stdout, "\n");

    // give warning if sample points clipped

    if (numClipped > 0) {
        sprintf(MsgStr,
                "WARNING: %d Metropolis samples clipped at search grid boundary.", numClipped);
        nll_putmsg(1, MsgStr);
    }


    // give warning if grid points rejected

    if (numGridReject > 0) {
        sprintf(MsgStr,
                "WARNING: %d Metropolis samples rejected; travel times for %d hypocenter test locations were not valid.",
                numGridReject, numGridReject);
        nll_putmsg(1, MsgStr);
    }

    for (nHypo = 0; nHypo < NumHypocenters; nHypo++) {

        phypo = DiffHypocenters + nHypo;

        // check reject location conditions

        // maximum like hypo on edge of grid
        if ((iBoundary = isOnGridBoundary(phypo->x, phypo->y, phypo->z,
                ptgrid, pMetrop->dx, pMetrop->dx, 0))) {
            sprintf(MsgStr,
                    "WARNING: max prob location on grid boundary %d, rejecting location.", iBoundary);
            nll_putmsg(1, MsgStr);
            sprintf(phypo->locStatComm, "%s", MsgStr);
            sprintf(phypo->locStat, "REJECTED");
        }

        // construct search information string
        sprintf(phypo->searchInfo,
                "METROPOLIS nSamp %ld nAcc %d nSave %d nClip %d Dstep0 %lf Dstep %lf",
                nGenerated, phypo->nSamples, phypo->nScatterSaved, phypo->numClipped,
                dx_init, metrop_dx[nHypo]);
        // write message
        nll_putmsg(3, phypo->searchInfo);


    }


    return (nSamplesTotal);

}




/*** function to create next metropolis sample */

/* move sample random distance and direction */

int DiffLocGetNextMetropolisSample(WalkParams* pMetrop, double dx, double xmin, double xmax,
        double ymin, double ymax, double zmin, double zmax,
        double* pxval, double* pyval, double* pzval, double* ptval) {

    int iClip = 0;
    double valx, valy, valz, valt, valsum, norm;
    double x, y, z, t;


    /* get unit vector in random direction */

    do {
        valx = get_rand_double(-1.0, 1.0);
        valy = get_rand_double(-1.0, 1.0);
        valz = get_rand_double(-1.0, 1.0);
        valt = get_rand_double(-1.0, 1.0);
        valsum = valx * valx + valy * valy + valz * valz + valt * valt;
    } while (valsum < SMALL_DOUBLE);

    norm = dx / sqrt(valsum);

    /* add step to last sample location */

    x = pMetrop->x + norm * valx;
    y = pMetrop->y + norm * valy;
    z = pMetrop->z + norm * valz;
    t = pMetrop->dt + norm * valt / pMetrop->velocity;


    /* crude clip against grid boundary */
    /* clip needed because travel time lookup requires that
                    location is within initial search grid */
    if (x < xmin) {
        x = xmin;
        iClip = 1;
    } else if (x > xmax) {
        x = xmax;
        iClip = 1;
    }
    if (y < ymin) {
        y = ymin;
        iClip = 1;
    } else if (y > ymax) {
        y = ymax;
        iClip = 1;
    }
    if (z < zmin) {
        z = zmin;
        iClip = 1;
    } else if (z > zmax) {
        z = zmax;
        iClip = 1;
    }


    /* update sample location */

    *pxval = x;
    *pyval = y;
    *pzval = z;
    *ptval = t;

    return (iClip);

}

/*** function to test new metropolis string */

int DiffLocMetropolisTest(double value_last, double value_new, double exp_last, double exp_new) {

    double prob;


    // check for near zero probabilities
    if (fabs(exp_last) < SMALLEST_LIKELIHOOD)
        return (1); // accept:

    // check for near zero probabilities
    if (fabs(exp_new) < SMALLEST_LIKELIHOOD)
        return (0); // do not accept:

    // compare with last sample using Mosegaard & Tarantola eq (17)

    if (value_new > value_last)
        return (1);

    if ((prob = get_rand_double(0.0, 1.0)) < exp_new / exp_last)
        return (1);
    else
        return (0);

}

/*** function to calculate probability density for differential locations */


double DiffLocCalcSolutionQuality(
        Vect3D hypo_test, double dtime,
        int nHypo, int num_hypos, HypoDesc* hypos,
        int num_arrivals, ArrivalDesc* arrival,
        GaussLocParams* gauss_par, int itype, double temperature, double* pmisfit, double* potime, int* pnReject, int isave) {

    if (LocMethod == METH_GAU_ANALYTIC) {
        double norm = 2.0;
        return (DiffLocCalcSolutionQuality_LN_NORM(norm,
                hypo_test, dtime, nHypo, num_hypos, hypos, num_arrivals, arrival,
                gauss_par, itype, temperature, pmisfit, potime, pnReject, isave));
    } else if (LocMethod == METH_L1_NORM) {
        double norm = 1.0;
        return (DiffLocCalcSolutionQuality_LN_NORM(norm,
                hypo_test, dtime, nHypo, num_hypos, hypos, num_arrivals, arrival,
                gauss_par, itype, temperature, pmisfit, potime, pnReject, isave));
    } else {
        return (-1.0);
    }

}




/*** function to calculate probability density */

/*		(MEN92, eq. 14) */

double DiffLocCalcSolutionQuality_LN_NORM(double norm,
        Vect3D hypo_test, double dtime,
        int nHypo, int num_hypos, HypoDesc* hypos,
        int num_arrivals, ArrivalDesc* arrival,
        GaussLocParams* gauss_par, int itype, double temperature, double* pmisfit, double* potime, int* pnReject, int isave) {

    int narr, nHypoOther;

    double weight, weight_sum;
    double misfit_sum, misfit_like;
    // hypoDD 	double misfit_ave;
    double mftemp;
    double ln_prob_density, rms_misfit;

    ArrivalDesc* parr;
    Vect3D hypo1, hypo2;
    double travel_time_diff, double_diff;
    double dotime1, dotime2;

    // isave
    int n_compan;
    static char filename[FILENAME_MAX];
    HypoDesc* phypo = NULL;


    if (isave) {
        phypo = DiffHypocenters + nHypo;
        phypo->nreadings = 0;
    }


    // calculate residuals

    *pnReject = 0;
    misfit_sum = 0.0;
    //hypoDD misfit_ave = 0.0;
    misfit_like = 0.0;
    weight_sum = 0.0;
    for (narr = 0; narr < num_arrivals; narr++) {

        parr = arrival + narr;

        if (parr->flag_ignore)
            continue;

        //printf("nHypo %d parr->dd_event_index_1,2  %d,%d\n", nHypo, parr->dd_event_index_1, parr->dd_event_index_2);

        // check if target hypo concerns this arrival and set hypocenter 1 and 2 coords
        //printf("nHypo %d parr->dd_event_index_1,2  %d,%d\n", nHypo, parr->dd_event_index_1, parr->dd_event_index_2);
        if (parr->dd_event_index_1 == nHypo) {
            nHypoOther = parr->dd_event_index_2;
            hypo1 = hypo_test;
            hypo2.x = hypos[nHypoOther].x;
            hypo2.y = hypos[nHypoOther].y;
            hypo2.z = hypos[nHypoOther].z;
            dotime1 = dtime;
            dotime2 = hypos[nHypoOther].dotime;
        } else if (parr->dd_event_index_2 == nHypo) {
            nHypoOther = parr->dd_event_index_1;
            hypo2 = hypo_test;
            hypo1.x = hypos[nHypoOther].x;
            hypo1.y = hypos[nHypoOther].y;
            hypo1.z = hypos[nHypoOther].z;
            dotime1 = hypos[nHypoOther].dotime;
            dotime2 = dtime;
        } else {
            continue;
        }
        //printf("FOUND!!!\n");

        // get travel time difference
        travel_time_diff = getTravelTimeDiff(arrival, narr, hypo1, hypo2);
        //printf("nHypo %d  travel_time_diff %lf\n", nHypo, travel_time_diff);
        if (travel_time_diff < -LARGE_DOUBLE) {
            if (isave) {
                arrival[narr].pred_travel_time = 0.0;
                arrival[narr].cent_resid = 0.0;
            }
            *pnReject = 1;
            return (-1.0);
        }

        // construct the double difference residual
        // dr =   ( [Txcorr12-[OT1-OT2]] -   [dOT1 - dOT2] )    -  ( TT1 - TT2 )
        double_diff = (parr->dd_dtime - (dotime1 - dotime2)) - travel_time_diff;

        // accumulate L2 misfit
        weight = parr->weight;
        weight_sum += pow(weight, norm);
        mftemp = weight * fabs(double_diff);
        misfit_sum += pow(mftemp, norm);
        if (parr->xcorr_flag)
            misfit_like += pow(mftemp / (xcorr_uncertainty * temperature), norm);
        else
            misfit_like += pow(mftemp / (cat_uncertainty * temperature), norm);

        // if saving solution
        if (isave) {
            hypos[nHypo].nreadings++;
            arrival[narr].pred_travel_time = travel_time_diff;
            arrival[narr].residual = double_diff;
            if (parr->dd_event_index_1 == nHypo) {
                arrival[narr].dist = GetEpiDist(
                        &(arrival[narr].station), phypo->x, phypo->y);
                if (GeometryMode == MODE_GLOBAL)
                    arrival[narr].dist *= KM2DEG;
                arrival[narr].azim = GetEpiAzim(
                        &(arrival[narr].station), phypo->x, phypo->y);
            }
            // read angles
            if (angleMode == ANGLE_MODE_YES) {
                n_compan = arrival[narr].n_companion;
                if (n_compan >= 0)
                    sprintf(filename, "%s.angle", arrival[n_compan].fileroot);
                else
                    sprintf(filename, "%s.angle", arrival[narr].fileroot);
                if (arrival[narr].gdesc.type == GRID_TIME) {
                    // 3D grid
                    ReadTakeOffAnglesFile(filename,
                            phypo->x, phypo->y, phypo->z,
                            &(arrival[narr].ray_azim),
                            &(arrival[narr].ray_dip),
                            &(arrival[narr].ray_qual), -1.0, iSwapBytesOnInput);
                } else {
                    // 2D grid (1D model)
                    ReadTakeOffAnglesFile(filename,
                            0.0, arrival[narr].dist, phypo->z,
                            &(arrival[narr].ray_azim),
                            &(arrival[narr].ray_dip),
                            &(arrival[narr].ray_qual), arrival[narr].azim, iSwapBytesOnInput);
                }
            }
        }


    }


    if (fabs(weight_sum) < SMALL_DOUBLE) {
        *pnReject = 1;
        return (-1.0);
    }

    // return misfit or ln(prob density)

    // convert misfit_sum to (pseudo, if not L2 norm) rms misfit
    rms_misfit = pow(misfit_sum / weight_sum, 1.0 / norm);
    if (isave) {
        hypos[nHypo].rms = rms_misfit; // pseudo rms, if not L2 norm
    }
    if (itype == GRID_MISFIT) {
        *pmisfit = rms_misfit;
        return (rms_misfit);
    } else if (itype == GRID_PROB_DENSITY) {
        ln_prob_density = -(1.0 / norm) * misfit_like;
        *pmisfit = rms_misfit;
        return (ln_prob_density);
    } else {
        return (-1.0);
    }



}

/** function to get difference in travel times for one arrival for two events (T1 - T2) */

double getTravelTimeDiff(ArrivalDesc* arrival, int narr, Vect3D hypo1, Vect3D hypo2) {

    int n_compan;
    FILE* fp_grid;
    double yval_grid;
    double travel_time1, travel_time2;
    GridDesc* ptgrid;
    ArrivalDesc* arr;


    arr = arrival + narr;
    /* check for companion */
    if ((n_compan = arr->n_companion) >= 0) {
        arr = arrival + n_compan;
    }

    /* get times for 2 events */

    if (arr->gdesc.type == GRID_TIME) {
        /* 3D grid */
        if (arr->gdesc.buffer == NULL) {
            /* read time grid from disk */
            fp_grid = arr->fpgrid;
        } else {
            /* read time grid from memory buffer */
            fp_grid = NULL;
        }
        // hypo1
        travel_time1 = (double) ReadAbsInterpGrid3d(fp_grid, &(arr->gdesc), hypo1.x, hypo1.y, hypo1.z);
        if (travel_time1 < 0.0)
            return (-LARGE_DOUBLE * 2.0);
        // hypo2
        travel_time2 = (double) ReadAbsInterpGrid3d(fp_grid, &(arr->gdesc), hypo2.x, hypo2.y, hypo2.z);
        if (travel_time2 < 0.0)
            return (-LARGE_DOUBLE * 2.0);
    } else {
        /* 2D grid (1D model) */
        if (arr->sheetdesc.buffer == NULL) {
            /* read time grid from disk */
            fp_grid = arr->fpgrid;
            ptgrid = &(arr->gdesc);
        } else {
            /* read time grid from memory buffer */
            fp_grid = NULL;
            ptgrid = &(arr->sheetdesc);
        }
        // hypo1
        yval_grid = GetEpiDist(&(arr->station), hypo1.x, hypo1.y);
        if (GeometryMode == MODE_GLOBAL)
            yval_grid *= KM2DEG;
        travel_time1 = ReadAbsInterpGrid2d(fp_grid, ptgrid, yval_grid, hypo1.z);
        if (travel_time1 < 0.0) {
            //printf("narr %d  travel_time1 %lf  yval_grid %lf  hypo1.z %lf  (arrival + narr)->tfact %f  ptgrid %s\n",
            //narr, travel_time2,  yval_grid, hypo1.z, (arrival + narr)->tfact, ptgrid->title);
            return (-LARGE_DOUBLE * 2.0);
        }
        // hypo2
        yval_grid = GetEpiDist(&(arr->station), hypo2.x, hypo2.y);
        if (GeometryMode == MODE_GLOBAL)
            yval_grid *= KM2DEG;
        travel_time2 = ReadAbsInterpGrid2d(fp_grid, ptgrid, yval_grid, hypo2.z);
        if (travel_time2 < 0.0) {
            //printf("narr %d  travel_time2 %lf  yval_grid %lf  hypo2.z %lf  (arrival + narr)->tfact %f  ptgrid %s\n",
            //narr, travel_time2,  yval_grid, hypo2.z, (arrival + narr)->tfact, ptgrid->title);
            return (-LARGE_DOUBLE * 2.0);
        }
    }

    return ((travel_time1 - travel_time2) * (arrival + narr)->tfact);

}





/** function to re-calculate solution and arrival statistics for best location */

/* some quantities are calculated only for arrivals used in location
                (num_arr_loc) others for all arrivals (num_arr_total) */

int DiffLocSaveBestLocation(int num_arr_total, int num_arr_loc, ArrivalDesc *arrival,
        GridDesc* ptgrid, GaussLocParams* gauss_par, int nHypo, int iGridType) {

    int istat, nReject;
    double value, misfit;
    HypoDesc* phypo;

    //DD
    Vect3D hyp_xyz;

    // 20110725 AJL - added temperature
    double temperature = 1.0;

    phypo = DiffHypocenters + nHypo;
    hyp_xyz.x = phypo->x;
    hyp_xyz.y = phypo->y;
    hyp_xyz.z = phypo->z;
    // re-calculate solution
    value = DiffLocCalcSolutionQuality(hyp_xyz, phypo->dotime,
            nHypo, NumHypocenters, DiffHypocenters,
            num_arr_loc, arrival, gauss_par,
            iGridType, temperature, &misfit, NULL, &nReject, 1);
    if (nReject) {
        phypo->misfit = HUGE_MISFIT;
        phypo->probmax = 0.0;
    } else {
        phypo->misfit = misfit;
        phypo->probmax = exp(value);
    }

    // set origin time
    if (!FixOriginTimeFlag)
        phypo->sec += phypo->dotime;

    /* set misc hypo fields */
    istat = rect2latlon(0, phypo->x, phypo->y, &(phypo->dlat), &(phypo->dlong));
    phypo->depth = phypo->z;

    return (0);

}




/*** function to find and assign HypoDesc indices for events in each arrival */

/*		(MEN92, eq. 14) */

int AssignEventIndexes(
        int num_hypos, HypoDesc* hypos,
        int num_arrivals, ArrivalDesc *arrival, double *pmean_abs, int *pnumberP, int *pnumberS, int *pnumberOther) {

    int narr, nhyp;
    int nIgnore, found1, found2;
    long int event_id, event1, event2;
    double mean_sum = 0.0;
    int nmean = 0;
    ArrivalDesc* parr;
    HypoDesc *phyp, *phyp1 = NULL, *phyp2 = NULL;


    /* find and assign HypoDesc indices for events in each arrival */

    nIgnore = 0;
    *pnumberP = *pnumberS = *pnumberOther = 0;
    parr = arrival;
    for (narr = 0; narr < num_arrivals; narr++) {

        //printf("%s dd:%lf %lf %s ignore:%d\n", arrival->label, arrival->dd_dtime, arrival->weight, arrival->phase, arrival->flag_ignore);

        if (parr->flag_ignore) {
            nIgnore++;
            if (message_flag >= 3) {
                sprintf(MsgStr,
                        "AssignEventIndexes: IGNORE arrival flag_ignore %s %s events: %ld %ld\n",
                        parr->label, parr->phase, parr->dd_event_id_1, parr->dd_event_id_2);
                nll_putmsg(3, MsgStr);
            }
            parr++;
            continue;
        }

        event1 = parr->dd_event_id_1;
        event2 = parr->dd_event_id_2;
        found1 = found2 = 0;
        phyp = hypos;
        for (nhyp = 0; nhyp < num_hypos; nhyp++) {
            //printf("TP AEI 1 - nhyp %d  phyp->event_id %d  event1 %d  event2 %d\n", nhyp, phyp->event_id, event1, event2);
            event_id = phyp->event_id;
            if (event_id == event1) {
                parr->dd_event_index_1 = nhyp;
                //printf("TP AEI 2 - found1 phyp->event_id %d == event1 %d\n", phyp->event_id, event1);
                phyp1 = phyp;
                found1 = 1;
                if (found2)
                    break;
            } else if (event_id == event2) {
                parr->dd_event_index_2 = nhyp;
                //printf("TP AEI 3 - found2 phyp->event_id %d == event2 %d\n", phyp->event_id, event2);
                phyp2 = phyp;
                found2 = 1;
                if (found1)
                    break;
            }
            phyp++;
        }
        if (found1 && found2) {
            phyp1->flag_ignore = 0;
            phyp2->flag_ignore = 0;
            mean_sum += fabs(parr->dd_dtime);
            nmean++;
            if (IsPhaseID(parr->phase, "P"))
                (*pnumberP)++;
            else if (IsPhaseID(parr->phase, "S"))
                (*pnumberS)++;
            else
                (*pnumberOther)++;

        } else {
            parr->flag_ignore = 9;
            nIgnore++;
            if (message_flag >= 3) {
                sprintf(MsgStr,
                        "AssignEventIndexes: IGNORE events not found: %s %s events: %ld %ld\n",
                        parr->label, parr->phase, parr->dd_event_id_1, parr->dd_event_id_2);
                nll_putmsg(3, MsgStr);
            }
        }

        parr++;
    }

    *pmean_abs = mean_sum / (double) nmean;

    return (nIgnore);

}

/*** function to write differential time event links in GMT graphics xyz format */

int SaveDiffTimeLinks(int num_hypos, HypoDesc* hypos, int num_arrivals, ArrivalDesc* arrival, FILE* fp_out) {

    int narr;
    ArrivalDesc* parr;
    HypoDesc *phypo1, *phypo2;

    fprintf(fp_out, "> GMT_LATLONDEPTH\n");

    // loop through arrivals and write valid event links to file

    int nwritten = 0;

    for (narr = 0; narr < num_arrivals; narr++) {

        parr = arrival + narr;
        if (parr->flag_ignore)
            continue;

        phypo1 = hypos + parr->dd_event_index_1;
        if (phypo1->flag_ignore)
            continue;
        phypo2 = hypos + parr->dd_event_index_2;
        if (phypo2->flag_ignore)
            continue;

        fprintf(fp_out, "> %d %d\n", parr->dd_event_index_1, parr->dd_event_index_2);
        fprintf(fp_out, "%f %f %f\n", phypo1->dlat, phypo1->dlong, phypo1->depth);
        fprintf(fp_out, "%f %f %f\n", phypo2->dlat, phypo2->dlong, phypo2->depth);

        nwritten++;

    }

    return (nwritten);

}



/*** function to write hypoDD format Data residual output (e.g. file hypoDD.res) */
/*



STA         Station label.
DT          Delay time.
ID1, ID2    ID of event 1, event 2.
IDX         Data type (1=ccP; 2=ccS; 3=ctP; 4=ctS).
WGHT        A priori weight.
RES         Data residual (ms).
WT          Weight after last iteration. DIST Inter-event distance (m).
 */

/*STA           DT        C1        C2    IDX     QUAL    RES [ms]   WT         OFFS
NCCAI      0.0151391     38542     38520 1    0.9600     1.408186    0.959618     56.8
NCCBR      0.0021439     38542     38520 1    0.9700    -2.560102    0.968049     56.8
NCCDO     -0.0002154     38542     38520 1    0.7200    -4.250846    0.713306     56.8
...
 */
int SaveHypoDDRes(int num_hypos, HypoDesc* hypos, int num_arrivals, ArrivalDesc* arrival, FILE* fp_out) {

    int narr;
    ArrivalDesc* parr;
    HypoDesc *phypo1, *phypo2;

    fprintf(fp_out, "STA   DT   ID1   ID2   IDX   WGHT   RES[ms]   WT   DIST[m]\n");

    // loop through arrivals and write valid event links to file

    int nwritten = 0;

    for (narr = 0; narr < num_arrivals; narr++) {

        parr = arrival + narr;
        if (parr->flag_ignore)
            continue;

        phypo1 = hypos + parr->dd_event_index_1;
        if (phypo1->flag_ignore)
            continue;
        phypo2 = hypos + parr->dd_event_index_2;
        if (phypo2->flag_ignore)
            continue;

        //IDX         Data type (1=ccP; 2=ccS; 3=ctP; 4=ctS).
        int idx = -1;
        if (parr->xcorr_flag) {
            if (strncmp(parr->phase, "P", 1) == 0) {
                idx = 1;
            } else if (strncmp(parr->phase, "S", 1) == 0) {
                idx = 2;
            }
        } else {
            if (strncmp(parr->phase, "P", 1) == 0) {
                idx = 3;
            } else if (strncmp(parr->phase, "S", 1) == 0) {
                idx = 4;
            }
        }

        // DIST
        double dist3D = Dist3D(phypo1->x, phypo2->x, phypo1->y, phypo2->y, phypo1->depth, phypo2->depth);

        // STA, DT, ID1, ID2, IDX, WGHT, RES[ms], WT, DIST[m]
        fprintf(fp_out, "%s %f %d %d %d %f %f %f %f\n",
                parr->label,
                parr->dd_dtime,
                parr->dd_event_index_1, parr->dd_event_index_2,
                idx,
                parr->weight,
                parr->residual * 1000.0,
                parr->weight,
                dist3D * 1000.0
                );

        nwritten++;

    }

    return (nwritten);

}

































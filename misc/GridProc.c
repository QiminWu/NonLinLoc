/*
 * Copyright (C) 1999 Anthony Lomax <lomax@faille.unice.fr>
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


/*   GridProc.c

        Program to process (add, ) 3D grid files

 */

//------------------------------------------------------------/
// Anthony Lomax           | email: lomax@faille.unice.fr     /
// UMR Geosciences Azur    | web: www-geoazur.unice.fr/~lomax /
// 250 Rue Albert Einstein | tel: 33 (0) 4 93 95 43 25        /
// 06560 Valbonne, FRANCE  | fax: 33 (0) 4 93 65 27 17        /
//------------------------------------------------------------/


/*
        history:

        ver 01    04Nov1997  AJL  Original version


.........1.........2.........3.........4.........5.........6.........7.........8

 */



#include "GridLib.h"


// defines

#define MAX_NUM_INPUT_FILES 500


// globals


// functions

int DoProcess(int argc, char *argv[], int process_type);



/*** Program to process (add, ) 3D grid files */

#define PNAME  "GridProc"

#define PROC_ADD 0
#define PROC_SUB 1

int main(int argc, char *argv[]) {

    int istat, narg, nchar;
    char process[MAXLINE];


    // set program name

    strcpy(prog_name, PNAME);


    // check command line for correct usage

    fprintf(stdout, "\n%s Arguments: ", prog_name);
    for (narg = 0; narg < argc; narg++)
        fprintf(stdout, "<%s> ", argv[narg]);
    fprintf(stdout, "\n");

    if (argc > 1)
        sscanf(argv[1], "%s", process);
    else
        strcpy(process, "");
    nchar = 0;
    while (nchar < MAXLINE && (process[nchar] = toupper(process[nchar])))
        nchar++;

    if (strcmp(process, "ADD") == 0) {
        if (argc < 4) {
            nll_puterr("ERROR wrong number of command line arguments.");
            disp_usage(PNAME,
                    "ADD <size_gridfile> <output_gridfile> <add_grdfile_list>");
            exit(-1);
        }
        if ((istat = DoProcess(argc, argv, PROC_ADD)) < 0) {
            nll_puterr("ERROR doing ADD process.");
            exit(-1);
        }
    } else if (strcmp(process, "SUB") == 0) {
        if (argc < 5) {
            nll_puterr("ERROR wrong number of command line arguments.");
            disp_usage(PNAME,
                    "SUB <size_gridfile> <output_gridfile> <left_grdfile> <right_grdfile>");
            exit(-1);
        }
        if ((istat = DoProcess(argc, argv, PROC_SUB)) < 0) {
            nll_puterr("ERROR doing ADD process.");
            exit(-1);
        }
    } else {
        sprintf(MsgStr,
                "ERROR unrcognized process - <%s>.", process);
        nll_puterr(MsgStr);
        disp_usage(PNAME, "ADD/SUB... <arguments>");
        exit(-1);
    }



    exit(0);

}

int DoProcess(int argc, char *argv[], int process_type) {
    int istat;

    char fn_grid_size[FILENAME_MAX];
    char fn_grid_out[FILENAME_MAX], fn_grid_in[FILENAME_MAX];
    FILE *fp_grid_size, *fp_grid_in;
    FILE *fp_grid_size_hdr, *fp_grid_in_hdr;

    GridDesc grid_size, grid_out, grid_in;

    int nFile, numFiles;
    char fn_grid_in_list[MAX_NUM_INPUT_FILES][FILENAME_MAX];
    char *chrpos, test_str[] = ".buf";

    SourceDesc srce;

    // open size grid file

    strcpy(fn_grid_size, argv[2]);
    if ((istat = OpenGrid3dFile(fn_grid_size, &fp_grid_size,
            &fp_grid_size_hdr,
            &grid_size, "", &srce, 0)) < 0) {
        nll_puterr("ERROR opening size grid file.");
        return (-1);
    }



    // cread and initialize output grid

    // output file name
    strcpy(fn_grid_out, argv[3]);

    // ouput has same dimensions as size grid
    grid_out = grid_size;

    // allocate grid
    grid_out.buffer = AllocateGrid(&grid_out);
    if (grid_out.buffer == NULL) {
        nll_puterr(
                "ERROR: allocating memory for output grid buffer.\n");
        return (-1);
    }

    // create grid array access pointers
    grid_out.array = CreateGridArray(&grid_out);
    if (grid_out.array == NULL) {
        nll_puterr(
                "ERROR: creating array for accessing output grid buffer.\n");
        return (-1);
    }

    // initialize output grid to all zeros

    InitializeGrid(&grid_out, 0.0F);



    // sum requested grid files into output grid

    // input file name
    strcpy(fn_grid_in, argv[4]);

    // check for wildcards in input file name
    strcat(fn_grid_in, test_str);
    numFiles = ExpandWildCards(fn_grid_in, fn_grid_in_list, MAX_NUM_INPUT_FILES);
    if (process_type == PROC_SUB && numFiles > 1) {
        sprintf(MsgStr, "ERROR: cannot differenc more than 2 grids.");
        nll_puterr(MsgStr);
    }

    // get second input file for SUB difference
    if (process_type == PROC_SUB && numFiles == 1) {
        strcpy(fn_grid_in_list[1], argv[5]);
        numFiles = 2;
    }


    double factor = 1.0;
    for (nFile = 0; nFile < numFiles; nFile++) {
        chrpos = strstr(fn_grid_in_list[nFile], test_str);
        if (chrpos != NULL)
            *chrpos = '\0';
        fprintf(OUT_LEVEL_1, "Adding grid file <%s>.\n",
                fn_grid_in_list[nFile]);
        if ((istat = OpenGrid3dFile(fn_grid_in_list[nFile],
                &fp_grid_in, &fp_grid_in_hdr,
                &grid_in, "", &srce, 0)) < 0) {
            sprintf(MsgStr, "ERROR: opening grid file <%s>.", fn_grid_in_list[nFile]);
            nll_puterr(MsgStr);
        } else {
            if ((istat = SumGrids(&grid_out, &grid_in, fp_grid_in, factor)) < 0) {
                sprintf(MsgStr, "ERROR: processing grid file <%s>.", fn_grid_in_list[nFile]);
                nll_puterr(MsgStr);
            }
            grid_out.type = grid_in.type;
            strcpy(grid_out.chr_type, grid_in.chr_type);
            if (process_type == PROC_SUB) {
                factor = -1.0;
            }
        }
    }


    // save sum grid to disk

    char filename_type[MAXLINE];
    if (process_type == PROC_SUB)
        strcpy(filename_type, "diff");
    else
        strcpy(filename_type, "sum");
    if ((istat = WriteGrid3dBuf(&grid_out, &srce, fn_grid_out, filename_type)) < 0) {
        sprintf(MsgStr, "ERROR: writing sum grid to disk: <%s>.", fn_grid_out);
        nll_puterr(MsgStr);
        return (-1);
    }


    fclose(fp_grid_size);
    fclose(fp_grid_size_hdr);
    fclose(fp_grid_in);
    fclose(fp_grid_in_hdr);

    return (0);

}



//------------------------------------------------------------/
// Anthony Lomax           | email: lomax@faille.unice.fr     /
// UMR Geosciences Azur    | web: www-geoazur.unice.fr/~lomax /
// 250 Rue Albert Einstein | tel: 33 (0) 4 93 95 43 25        /
// 06560 Valbonne, FRANCE  | fax: 33 (0) 4 93 65 27 17        /
//------------------------------------------------------------/


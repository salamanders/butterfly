/* Copyright (C) 2013-2016, The Regents of The University of Michigan.
All rights reserved.

This software was developed in the APRIL Robotics Lab under the
direction of Edwin Olson, ebolson@umich.edu. This software may be
available under alternative licensing terms; contact the address above.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the Regents of The University of Michigan.
*/

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <getopt.h>

#include <apriltag.h>
#include <tag36h11.h>
#include <tag25h9.h>
#include <tag16h5.h>
#include <tagCircle21h7.h>
#include <tagCircle49h12.h>
#include <tagCustom48h12.h>
#include <tagStandard41h12.h>
#include <tagStandard52h13.h>

#include <FreeImage.h>

int main(int argc, char *argv[])
{
    const char* usage =
        "usage: %s [options] <input files>\n"
        "\n"
        "Option                  Default value   Description\n"
        "  -h | --help          [ true ]       Show this help\n"
        "  -d | --debug         [ false ]      Enable debugging output (slow)\n"
        "  -q | --quiet         [ false ]      Reduce output\n"
        "  -f | --family        [ tag36h11 ]   Tag family to use\n"
        "  -i | --iters         [ 1 ]          Repeat processing on input set this many times\n"
        "  -t | --threads       [ 1 ]          Use this many CPU threads\n"
        "  -x | --decimate      [ 1.0 ]        Decimate input image by this factor\n"
        "  -m | --maxhamming    [ 1 ]          Max number of corrected bits. Larger values guzzle RAM\n"
        "  -b | --blur          [ 0.0 ]        Apply low-pass blur to input; negative sharpens\n"
        "  -0 | --refine-edges  [ true ]       Spend more time trying to align edges of tags\n"
        "  -v | --vnlog         [ false ]      Write output as a vnlog\n";

    struct option opts[] =
        {
         { "debug",         no_argument,       NULL, 'd' },
         { "quiet",         no_argument,       NULL, 'q' },
         { "family",        required_argument, NULL, 'f' },
         { "iters",         required_argument, NULL, 'i' },
         { "threads",       required_argument, NULL, 't' },
         { "decimate",      required_argument, NULL, 'x' },
         { "maxhamming",    required_argument, NULL, 'm' },
         { "blur",          required_argument, NULL, 'B' },
         { "refine-edges",  no_argument,       NULL, '0' },
         { "vnlog",         no_argument,       NULL, 'v' },
         { "help",          no_argument,       NULL, 'h' },
         {}
        };

    bool        debug         = false;
    bool        quiet         = false;
    const char* family        = "tag36h11";
    int         maxiters      = 1;
    int         threads       = 1;
    double      decimate      = 1.0;
    int         maxhamming    = 1;
    double      blur          = 0.0;
    bool        refine_edges  = true;
    bool        vnlog         = false;

    int opt;
    do
    {
        opt = getopt_long(argc, argv, "dqf:i:t:x:m:0vh", opts, NULL);
        switch(opt)
        {
        case -1: break;
        case 'd': debug         = true;         break;
        case 'q': quiet         = true;         break;
        case 'f': family        = optarg;       break;
        case 'i': maxiters      = atoi(optarg); break;
        case 't': threads       = atoi(optarg); break;
        case 'x': decimate      = atof(optarg); break;
        case 'm': maxhamming    = atoi(optarg); break;
        case 'B': blur          = atof(optarg); break;
        case '0': refine_edges  = true;         break;
        case 'v': vnlog         = true;         break;

        case 'h':
            printf(usage, argv[0]);
            return 0;
        case '?':
            fprintf(stderr, "Cmdline-parsing error!\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    } while( opt != -1 );


    apriltag_family_t *tf = NULL;

    if      (!strcmp(family, "tag36h11"))         tf = tag36h11_create();
    else if (!strcmp(family, "tag25h9"))          tf = tag25h9_create();
    else if (!strcmp(family, "tag16h5"))          tf = tag16h5_create();
    else if (!strcmp(family, "tagCircle21h7"))    tf = tagCircle21h7_create();
    else if (!strcmp(family, "tagCircle49h12"))   tf = tagCircle49h12_create();
    else if (!strcmp(family, "tagStandard41h12")) tf = tagStandard41h12_create();
    else if (!strcmp(family, "tagStandard52h13")) tf = tagStandard52h13_create();
    else if (!strcmp(family, "tagCustom48h12"))   tf = tagCustom48h12_create();
    else {
        fprintf(stderr, "Unrecognized tag family name. Use e.g. \"tag36h11\".\n");
        exit(-1);
    }

    apriltag_detector_t *td = apriltag_detector_create();
    apriltag_detector_add_family_bits(td, tf, maxhamming);
    td->quad_decimate       = decimate;
    td->quad_sigma          = blur;
    td->nthreads            = threads;
    td->debug               = debug;
    td->refine_edges        = refine_edges;

    const int hamm_hist_max = 10;

    if(vnlog)
        printf("# path Ndetections hamming margin id xc yc xlb ylb xrb yrb xrt yrt xlt ylt\n");

    for (int iter = 0; iter < maxiters; iter++) {

        int total_quads = 0;
        int total_hamm_hist[hamm_hist_max];
        memset(total_hamm_hist, 0, sizeof(total_hamm_hist));
        double total_time = 0;

        if (maxiters > 1)
            fprintf(stderr, "iter %d / %d\n", iter + 1, maxiters);

        for (; optind < argc; optind++) {

            int hamm_hist[hamm_hist_max];
            memset(hamm_hist, 0, sizeof(hamm_hist));

            char* path = argv[optind];
            if (!quiet && !vnlog)
                fprintf(stderr, "loading %s\n", path);

            FIBITMAP* fimgray;
            {
                FIBITMAP* fim = FreeImage_Load(FreeImage_GetFIFFromFilename(path), path, 0);
                if( fim == NULL )
                {
                    fprintf(stderr,"couldn't load %s\n", path);
                    continue;
                }
                fimgray = FreeImage_ConvertToGreyscale(fim);
                FreeImage_Unload(fim);
            }

            if( fimgray == NULL )
            {
                fprintf(stderr,"couldn't convert %s to grayscale\n", path);
                continue;
            }
            FreeImage_FlipVertical(fimgray);

            image_u8_t im = {.width  = FreeImage_GetWidth(fimgray),
                             .height = FreeImage_GetHeight(fimgray),
                             .stride = FreeImage_GetPitch(fimgray),
                             .buf    = FreeImage_GetBits(fimgray)};


            zarray_t *detections = apriltag_detector_detect(td, &im);
            if(vnlog)
                printf("%s %d - - - - - - - - - - - - -\n",
                       path,
                       zarray_size(detections));

            for (int i = 0; i < zarray_size(detections); i++) {
                apriltag_detection_t *det;
                zarray_get(detections, i, &det);

                if(vnlog)
                    printf("%s - %d %f %d %f %f %f %f %f %f %f %f %f %f\n",
                           path,
                           det->hamming,
                           det->decision_margin,
                           det->id,
                           det->c[0], det->c[1],
                           det->p[0][0], det->p[0][1],
                           det->p[1][0], det->p[1][1],
                           det->p[2][0], det->p[2][1],
                           det->p[3][0], det->p[3][1]);
                else if (!quiet)
                    printf("detection %3d: id (%2dx%2d)-%-4d, hamming %d, margin %8.3f\n",
                           i, det->family->nbits, det->family->h, det->id, det->hamming, det->decision_margin);

                hamm_hist[det->hamming]++;
                total_hamm_hist[det->hamming]++;
            }

            apriltag_detections_destroy(detections);

            if (!quiet && !vnlog)
            {
                timeprofile_display(td->tp);
                total_quads += td->nquads;
                fprintf(stderr,"hamm ");

                for (int i = 0; i < hamm_hist_max; i++)
                    fprintf(stderr,"%5d ", hamm_hist[i]);

                double t =  timeprofile_total_utime(td->tp) / 1.0E3;
                total_time += t;
                fprintf(stderr,"%12.3f ", t);
                fprintf(stderr,"%5d", td->nquads);
                fprintf(stderr,"\n");
            }

            FreeImage_Unload(fimgray);
        }

        if(!quiet && !vnlog)
        {
            fprintf(stderr,"Summary\n");
            fprintf(stderr,"hamm ");

            for (int i = 0; i < hamm_hist_max; i++)
                fprintf(stderr,"%5d ", total_hamm_hist[i]);
            fprintf(stderr,"%12.3f ", total_time);
            fprintf(stderr,"%5d", total_quads);
            fprintf(stderr,"\n");
        }
    }

    apriltag_detector_destroy(td);

    if      (!strcmp(family, "tag36h11"         )) tag36h11_destroy(tf);
    else if (!strcmp(family, "tag25h9"          )) tag25h9_destroy(tf);
    else if (!strcmp(family, "tag16h5"          )) tag16h5_destroy(tf);
    else if (!strcmp(family, "tagCircle21h7"    )) tagCircle21h7_destroy(tf);
    else if (!strcmp(family, "tagCircle49h12"   )) tagCircle49h12_destroy(tf);
    else if (!strcmp(family, "tagStandard41h12" )) tagStandard41h12_destroy(tf);
    else if (!strcmp(family, "tagStandard52h13" )) tagStandard52h13_destroy(tf);

    return 0;
}

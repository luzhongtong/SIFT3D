/* -----------------------------------------------------------------------------
 * mexImRead3D.c
 * -----------------------------------------------------------------------------
 * Copyright (c) 2015 Blaine Rister et al., see LICENSE for details.
 * -----------------------------------------------------------------------------
 * Mex interface to read 3D images.
 * -----------------------------------------------------------------------------
 */

#include "imutil.h"
#include "mexutil.h"
#include "mex.h"
#include "matrix.h"

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {

        Image im;
        const mxArray *mxPath, *mxIm;
        const char *path;

/* Clean up and print an error */
#define CLEAN_AND_QUIT(name, msg) { \
                im_free(&im); \
                err_msgu(name, msg); \
        }

	// Verify the number of inputs
	if (nrhs != 1)
                err_msgu("main:numInputs", "This function takes 1 input.");

        // Verify the number of outputs
        if (nlhs > 1) 
                err_msgu("main:numOutputs", "This function takes 1 output.");

        // Assign the inputs
        mxPath = prhs[0];

        // Initialize intermediates
        init_im(&im);

        // Get the path string
        if ((path = mxArrayToString(mxPath)) == NULL)
                CLEAN_AND_QUIT("main:getPath", "Failed to convert the input "
                        "to a string");

        // Load the image
        switch (im_read(path, &im)) {
                case SIFT3D_SUCCESS:
                        break;
                case SIFT3D_FILE_DOES_NOT_EXIST:
                        im_free(&im);
                        err_msg("main:dne", "File does not exist");
                        break;
                case SIFT3D_UNSUPPORTED_FILE_TYPE:
                        im_free(&im);
                        err_msg("main:unsupportedType", "Unsupported file "
                                "type");
                        break;
                default:
                        CLEAN_AND_QUIT("main:unexpected", "Unexpected error "
                                "reading the image");
        }

        // Convert the output to a MATLAB array
        if ((plhs[0] = im2mx(&im)) == NULL)
                CLEAN_AND_QUIT("main:im2mx", "Failed to convert image to an "
                        "mxArray");

        // Clean up
        im_free(&im);

#undef CLEAN_AND_QUIT
}


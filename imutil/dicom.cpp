/* -----------------------------------------------------------------------------
 * dicom.cpp 
 * -----------------------------------------------------------------------------
 * Copyright (c) 2015 Blaine Rister et al., see LICENSE for details.
 * -----------------------------------------------------------------------------
 * C-language wrapper for the DCMTK library.
 * -----------------------------------------------------------------------------
 */


/*----------------Include the very picky DCMTK----------------*/
#include "dcmtk/config/osconfig.h"    /* make sure OS specific configuration is included first */

#define INCLUDE_CSTDIO
#define INCLUDE_CSTRING
#include "dcmtk/ofstd/ofstdinc.h"

#ifdef HAVE_GUSI_H
#include <GUSI.h>
#endif

#include "dcmtk/dcmdata/dctk.h"          /* for various dcmdata headers */
#include "dcmtk/dcmdata/cmdlnarg.h"      /* for prepareCmdLineArgs */
#include "dcmtk/dcmdata/dcuid.h"         /* for dcmtk version name */

#include "dcmtk/ofstd/ofconapp.h"        /* for OFConsoleApplication */
#include "dcmtk/ofstd/ofcmdln.h"         /* for OFCommandLine */

#include "dcmtk/oflog/oflog.h"           /* for OFLogger */

#include "dcmtk/dcmimgle/dcmimage.h"     /* for DicomImage */
#include "dcmtk/dcmimage/diregist.h"     /* include to support color images */
#include "dcmtk/dcmdata/dcrledrg.h"      /* for DcmRLEDecoderRegistration */

#ifdef BUILD_DCMSCALE_AS_DCMJSCAL
#include "dcmtk/dcmjpeg/djdecode.h"      /* for dcmjpeg decoders */
#include "dcmtk/dcmjpeg/dipijpeg.h"      /* for dcmimage JPEG plugin */
#endif

#include "dcmtk/dcmjpeg/djencode.h" /* for JPEG encoding */
#include "dcmtk/dcmjpeg/djrplol.h"  /* for DJ_RPLossless */

#include "dcmtk/dcmsr/dsrdoc.h" /* DSR report handling */

#ifdef WITH_ZLIB
#include <zlib.h>          /* for zlibVersion() */
#endif
/*---------------------------------------------------------*/

/* Other includes */
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
#include <cmath>
#include <cfloat>
#include <stdint.h>
#include <dirent.h>
#include "imutil.h"
#include "macros.h"
#include "dicom.h"

/* Macro to call a C++ function and catch any exceptions it throws,
 * returning SIFT3D_FAILURE when an exception is caught. The return value is
 * stored in ret. */
#define CATCH_EXCEPTIONS(ret, tag, fun, ...) \
        try { \
                ret = (fun)( __VA_ARGS__ ); \
        } catch (std::exception &e) { \
                std::cerr << tag << ": " << e.what() << std::endl; \
                ret = SIFT3D_FAILURE; \
        } catch (...) { \
                std::cerr << tag << ": unexpected exception " << std::endl; \
                ret = SIFT3D_FAILURE; \
        } \

/* File spearator in string form */
const std::string sepStr(1, SIFT3D_FILE_SEP);

/* Dicom parameteres */
const unsigned int dcm_bit_width = 8; // Bits per pixel

/* DICOM metadata defaults */
const char *default_patient_name = "DefaultSIFT3DPatient";
const char *default_series_descrip = "Series generated by SIFT3D";
const char *default_patient_id = "DefaultSIFT3DPatientID";
const char default_instance_num = 1;

/* Helper declarations */
static bool isLittleEndian(void);
static void default_Dcm_meta(Dcm_meta *const meta);
static int read_dcm_cpp(const char *path, Image *const im);
static int read_dcm_dir_cpp(const char *path, Image *const im);
static int write_dcm_cpp(const char *path, const Image *const im,
        const Dcm_meta *const meta, const float max_val);
static int write_dcm_dir_cpp(const char *path, const Image *const im,
        const Dcm_meta *const meta);
static void set_meta_defaults(const Dcm_meta *const meta, 
        Dcm_meta *const meta_new);

/* Helper class to store DICOM data. */
class Dicom {
private:
        std::string filename; // DICOM file name
        std::string seriesUID; // Series UID 
        int instance; // Instance number in the series
        double ux, uy, uz; // Voxel spacing in real-world coordinates
        int nx, ny, nz, nc; // Image dimensions
        bool valid; // Data validity 

public:

        /* Data is initially invalid */
        Dicom() : filename(""), seriesUID(""), instance(-1), valid(false) {};

        ~Dicom() {};

        /* Load a file */
        Dicom(std::string filename);

        /* Get the x-dimension */
        int getNx(void) const {
                return nx;
        }

        /* Get the y-dimension */
        int getNy(void) const {
                return ny;
        }

        /* Get the z-dimension */
        int getNz(void) const {
                return nz;
        }

        /* Get the number of channels */
        int getNc(void) const {
                return nc;
        } 

        /* Get the x-spacing */
        double getUx(void) const {
                return ux;
        }

        /* Get the y-spacing */
        double getUy(void) const {
                return uy;
        }

        /* Get the z-spacing */
        double getUz(void) const {
                return uz;
        }

        /* Check whether or not the data is valid */
        bool isValid(void) const {
                return valid;
        }

        /* Get the file name */
        std::string name(void) const {
                return filename;
        }

        /* Sort by instance number */
        bool operator < (const Dicom &dicom) const {
                return instance < dicom.instance;
        }

        /* Check if another DICOM file is from the same series */
        bool eqSeries(const Dicom &dicom) const {
                return !seriesUID.compare(dicom.seriesUID);
        }
};

/* Load the data from a DICOM file */
Dicom::Dicom(std::string path) : filename(path), valid(false) {

        // Load the image as a DcmFileFormat 
        DcmFileFormat fileFormat;
        OFCondition status = fileFormat.loadFile(path.c_str());
        if (status.bad()) {
               std::cerr << "Dicom.Dicom: failed to read DICOM file " <<
                        path << " (" << status.text() << ")" << 
                        std::endl; 
                return;
        }

        // Get the dataset
        DcmDataset *const data = fileFormat.getDataset();

        // Get the series UID 
        const char *seriesUIDStr;
        status = data->findAndGetString(DCM_SeriesInstanceUID, seriesUIDStr);
        if (status.bad() || seriesUIDStr == NULL) {
                std::cerr << "Dicom.Dicom: failed to get SeriesInstanceUID " <<
                        "from file " << path << " (" << status.text() << ")" <<
                        std::endl;
                return;
        }
        seriesUID = std::string(seriesUIDStr); 

        // Get the instance number
        const char *instanceStr;
        status = data->findAndGetString(DCM_InstanceNumber, instanceStr);
        if (status.bad() || instanceStr == NULL) {
                std::cerr << "Dicom.Dicom: failed to get instance number " <<
                        "from file " << path << " (" << status.text() << ")" <<
                        std::endl;
                return;
        }
        instance = atoll(instanceStr);

        // Load the DicomImage object
        DicomImage dicomImage(path.c_str());
        if (dicomImage.getStatus() != EIS_Normal) {
               std::cerr << "Dicom.image: failed to open image " <<
                        filename << " (" << 
                        DicomImage::getString(dicomImage.getStatus()) << ")" <<
                        std::endl; 
                return;
        }

        // Check for color images
        if (!dicomImage.isMonochrome()) {
                std::cerr << "Dicom.Dicom: reading of color DICOM " <<
                        "images is not supported at this time" << std::endl;
                return;
        }
        nc = 1;

        // Read the dimensions
        nx = dicomImage.getWidth();
        ny = dicomImage.getHeight();
        nz = dicomImage.getFrameCount();
        if (nx < 1 || ny < 1 || nz < 1) {
                std::cerr << "Dicom.Dicom: invalid dimensions for file "
                        << filename << "(" << nx << ", " << ny << ", " << 
                        nz << ")" << std::endl;
                return;
        }

        // Read the pixel spacing
        Float64 pixelSpacing;
        status = data->findAndGetFloat64(DCM_PixelSpacing,
                pixelSpacing);
        if (status.bad()) {
                std::cerr << "Dicom.Dicom: failed to get pixel spacing " <<
                        "from file " << path << " (" << status.text() << ")" <<
                        std::endl;
                return;
        }
        ux = static_cast<double>(pixelSpacing);
        if (ux <= 0.0) {
                std::cerr << "Dicom.Dicom: file " << path << " has " <<
                        "invalid pixel spacing: " << ux << std::endl;
                return;
        }

        // Get the aspect ratio
        const double ratio = dicomImage.getHeightWidthRatio();
        uy = ux * ratio;
        if (uy <= 0.0) {
                std::cerr << "Dicom.Dicom: file " << path << " has invalid " <<
                        "pixel aspect ratio: " << ratio << std::endl;
                return;
        }

        // Read the slice thickness 
        Float64 sliceThickness;
        status = data->findAndGetFloat64(DCM_SliceThickness, sliceThickness);
        if (!status.good()) {
                std::cerr << "Dicom.Dicom: failed to get slice thickness " <<
                        "from file " << path << " (" << status.text() << ")" <<
                        std::endl;
                return;
        }

        // Convert to double 
        uz = sliceThickness;
        if (uz <= 0.0) {
                std::cerr << "Dicom.Dicom: file " << path << " has " <<
                        "invalid slice thickness: " << uz << std::endl;
                return;
        }
        
        // Set the window 
        dicomImage.setMinMaxWindow();

        valid = true;
}

/* Check the endianness of the machine. Returns true if the machine is little-
 * endian, false otherwise. */
static bool isLittleEndian(void) {
        volatile uint16_t i = 0x0123;
        return ((uint8_t *) &i)[0] == 0x23;
}

/* Set a Dcm_meta struct to default values. Generates new UIDs. */
static void default_Dcm_meta(Dcm_meta *const meta) {
        meta->patient_name = default_patient_name;
        meta->patient_id = default_patient_id;
        meta->series_descrip = default_series_descrip;
        dcmGenerateUniqueIdentifier(meta->study_uid, SITE_STUDY_UID_ROOT);
        dcmGenerateUniqueIdentifier(meta->series_uid, SITE_SERIES_UID_ROOT);
        dcmGenerateUniqueIdentifier(meta->instance_uid, SITE_INSTANCE_UID_ROOT); 
        meta->instance_num = default_instance_num;
}

/* Read a DICOM file into an Image struct. */
int read_dcm(const char *path, Image *const im) {

        int ret;

        CATCH_EXCEPTIONS(ret, "read_dcm", read_dcm_cpp, path, im);

        return ret;
}

/* Read all of the DICOM files from a directory into an Image struct. Slices 
 * must be ordered alphanumerically, starting with z = 0. */
int read_dcm_dir(const char *path, Image *const im) {

        int ret;

        CATCH_EXCEPTIONS(ret, "read_dcm_dir", read_dcm_dir_cpp, path, im);

        return ret;
}

/* Write an Image struct into a DICOM file. 
 * Inputs: 
 *      path - File name
 *      im - Image data
 *      meta - Dicom metadata (or NULL for default values)
 *      max_val - The maximum value of the image, used for scaling. If set
 *              to a negative number, this functions computes the maximum value
 *              from this image.
 *
 * Returns SIFT3D_SUCCESS on success, SIFT3D_FAILURE otherwise.
 */
int write_dcm(const char *path, const Image *const im, 
        const Dcm_meta *const meta, const float max_val) {

        int ret;

        CATCH_EXCEPTIONS(ret, "write_dcm", write_dcm_cpp, path, im, meta, 
                max_val);

        return ret;
}

/* Write an Image struct into a directory of DICOM files.
 * Inputs: 
 *      path - File name
 *      im - Image data
 *      meta - Dicom metadata (or NULL for default values)
 *
 * Returns SIFT3D_SUCCESS on success, SIFT3D_FAILURE otherwise.
 */
int write_dcm_dir(const char *path, const Image *const im, 
        const Dcm_meta *const meta) {

        int ret;

        CATCH_EXCEPTIONS(ret, "write_dcm_dir", write_dcm_dir_cpp, path, im, 
                meta);

        return ret;
}

/* Helper function to read a DICOM file using C++ */
static int read_dcm_cpp(const char *path, Image *const im) {

        // Read the image metadata
        Dicom dicom(path);
        if (!dicom.isValid())
                return SIFT3D_FAILURE;

        // Load the DicomImage object
        DicomImage dicomImage(path);
        if (dicomImage.getStatus() != EIS_Normal) {
               std::cerr << "read_dcm_cpp: failed to open image " <<
                        dicom.name() << " (" << 
                        DicomImage::getString(dicomImage.getStatus()) << ")" <<
                        std::endl; 
                return SIFT3D_FAILURE;
        }

        // Initialize the image fields
        im->nx = dicom.getNx();
        im->ny = dicom.getNy();
        im->nz = dicom.getNz();
        im->nc = dicom.getNc();
        im->ux = dicom.getUx();
        im->uy = dicom.getUy();
        im->uz = dicom.getUz();

        // Resize the output
        im_default_stride(im);
        if (im_resize(im))
                return SIFT3D_FAILURE;

        // Get the bit depth of the image
        const int bufNBits = 32;
        const int depth = dicomImage.getDepth();
        if (depth > bufNBits) {
                std::cerr << "read_dcm_cpp: buffer is insufficiently wide " <<
                        "for " << depth << "-bit data of image " << path <<
                        std::endl;
                return SIFT3D_FAILURE;
        }

        // Get the number of bits by which we need to shift the 32-bit data, to
        // recover the original resolution (DICOM uses Big-endian encoding)
        const uint32_t shift = isLittleEndian() ? 
                static_cast<uint32_t>(bufNBits - depth) : 0;

        // Read each frame
        for (int i = 0; i < im->nz; i++) { 

                // Get a pointer to the data, rendered as a 32-bit int
                const uint32_t *const frameData = 
                        static_cast<const uint32_t *const>(
                                dicomImage.getOutputData(
                                        static_cast<int>(bufNBits), i));
                if (frameData == NULL) {
                        std::cerr << "read_dcm_cpp: could not get data "
                                << "from image " << path << " frame " << i <<
                                " (" << 
                                DicomImage::getString(dicomImage.getStatus()) <<
                                ")" << std::endl; 
                        return SIFT3D_FAILURE;
                }

                // Copy the frame
                const int x_start = 0;
                const int y_start = 0;
                const int z_start = i;
                const int x_end = im->nx - 1;
                const int y_end = im->ny - 1;
                const int z_end = z_start;
                int x, y, z;
                SIFT3D_IM_LOOP_LIMITED_START(im, x, y, z, x_start, x_end,
                        y_start, y_end, z_start, z_end)

                        // Get the voxel and shift it to match the original
                        // magnitude 
                        const uint32_t vox =
                                frameData[x + y * im->nx] >> shift;

                        // Convert to float and write to the output image
                        SIFT3D_IM_GET_VOX(im, x, y, z, 0) =
                                static_cast<float>(vox);

                SIFT3D_IM_LOOP_END
        }

        return SIFT3D_SUCCESS;
}

/* Helper funciton to read a directory of DICOM files using C++ */
static int read_dcm_dir_cpp(const char *path, Image *const im) {

        struct stat st;
        DIR *dir;
        struct dirent *ent;
        int i, nx, ny, nz, nc, num_files, off_z;

        // Verify that the directory exists
	if (stat(path, &st)) {
                std::cerr << "read_dcm_dir_cpp: cannot find file " << path <<
                        std::endl;
                return SIFT3D_FAILURE;
	} else if (!S_ISDIR(st.st_mode)) {
                std::cerr << "read_dcm_dir_cpp: file " << path <<
                        " is not a directory" << std::endl;
                return SIFT3D_FAILURE;
	}

        // Open the directory
        if ((dir = opendir(path)) == NULL) {
                std::cerr << "read_dcm_dir_cpp: unexpected error opening " <<
                        "directory" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Get all of the .dcm files in the directory
        std::vector<Dicom> dicoms;
        while ((ent = readdir(dir)) != NULL) {

                // Form the full file path
                std::string fullfile(std::string(path) + sepStr + ent->d_name);

                // Check if it is a DICOM file 
                if (im_get_format(fullfile.c_str()) != DICOM)
                        continue;

                // Read the file
                Dicom dicom(fullfile);
                if (!dicom.isValid()) {
                        closedir(dir);
                        return SIFT3D_FAILURE;
                }

                // Add the file to the list
                dicoms.push_back(dicom);
        }

        // Release the directory
        closedir(dir);
        
        // Get the number of files
        num_files = dicoms.size();

        // Verify that dicom files were found
        if (num_files == 0) {
                std::cerr << "read_dcm_dir_cpp: no dicom files found in " <<
                        path << std::endl;
                return SIFT3D_FAILURE;
        }

        // Check that the files are from the same series
        const Dicom &first = dicoms[0];
        for (int i = 1; i < num_files; i++) {

                const Dicom &dicom = dicoms[i];

                if (!first.eqSeries(dicom)) {
                        std::cerr << "read_dcm_dir_cpp: file " << 
                                dicom.name() << 
                                " is from a different series than file " <<
                                first.name() << std::endl;
                        return SIFT3D_FAILURE;
                }
        }

        // Initialize the output dimensions
        nx = first.getNx();
        ny = first.getNy();
        nc = first.getNc();

        // Verify the dimensions of the other files, counting the total
        // series z-dimension
        nz = 0;
        for (i = 0; i < num_files; i++) {

                // Get a slice
                const Dicom &dicom = dicoms[i];        

                // Verify the dimensions
                if (dicom.getNx() != nx || dicom.getNy() != ny || 
                        dicom.getNc() != nc) {
                        std::cerr << "read_dcm_dir_cpp: slice " << 
                                dicom.name() <<
                                " (" << dicom.getNx() << "x, " << 
                                dicom.getNy() << "y, " << dicom.getNc() << 
                                "c) does not match the dimensions of slice " <<
                                first.name() << "(" << nx << "x, " << ny << 
                                "y, " << nc << "c). " << std::endl;
                        return SIFT3D_FAILURE;
                }

                // Count the z-dimension
                nz += dicom.getNz();
        }

        // Resize the output
        im->nx = nx;
        im->ny = ny;
        im->nz = nz;
        im->nc = nc;
        im->ux = first.getUx(); 
        im->uy = first.getUy();
        im->uz = first.getUz();
        im_default_stride(im);
        if (im_resize(im))
                return SIFT3D_FAILURE;

        // Sort the slices by instance number
        std::sort(dicoms.begin(), dicoms.end()); 

        // Allocate a temporary image for the slices
        Image slice;
        init_im(&slice);

        // Read the image data
        off_z = 0;
        for (i = 0; i < num_files; i++) {

                int x, y, z, c;

                const char *slicename = dicoms[i].name().c_str();

                // Read the slice 
                if (read_dcm(slicename, &slice)) {
                        im_free(&slice);
                        return SIFT3D_FAILURE;
                }

                // Copy the data to the volume
                SIFT3D_IM_LOOP_START_C(&slice, x, y, z, c)

                        SIFT3D_IM_GET_VOX(im, x, y, z + off_z, c) =
                                SIFT3D_IM_GET_VOX(&slice, x, y, z, c);

                SIFT3D_IM_LOOP_END_C

                off_z += slice.nz;
        }
        assert(off_z == nz);
        im_free(&slice);

        return SIFT3D_SUCCESS;
} 

/* Helper function to set meta_new to default values if meta is NULL,
 * otherwise copy meta to meta_new */
static void set_meta_defaults(const Dcm_meta *const meta, 
        Dcm_meta *const meta_new) {
        if (meta == NULL) {
                default_Dcm_meta(meta_new);
        } else {
                *meta_new = *meta;        
        }
}

/* Helper function to write a DICOM file using C++ */
static int write_dcm_cpp(const char *path, const Image *const im,
        const Dcm_meta *const meta, const float max_val) {

#define BUF_LEN 1024
        char buf[BUF_LEN];

        // Ensure the image is monochromatic
        if (im->nc != 1) {
                std::cerr << "write_dcm_cpp: image has " << im->nc <<
                        " channels. Currently only single-channel images " <<
                        "are supported." << std::endl;
                return SIFT3D_FAILURE;
        }

        // If no metadata was provided, initialize default metadata
        Dcm_meta meta_new;
        set_meta_defaults(meta, &meta_new);

        // Create a new fileformat object
        DcmFileFormat fileFormat;

        // Set the file type to derived
        DcmDataset *const dataset = fileFormat.getDataset();
        OFCondition status = dataset->putAndInsertString(DCM_ImageType, 
                                                         "DERIVED");
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the image type" <<
                        std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the class UID
        dataset->putAndInsertString(DCM_SOPClassUID, 
                UID_CTImageStorage);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the SOPClassUID" <<
                        std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the photometric interpretation
        const char *photoInterp;
        if (im->nc == 1) {
                photoInterp = "MONOCHROME2";
        } else if (im->nc == 3) {
                photoInterp = "RGB";
        } else {
                std::cerr << "write_dcm_cpp: Failed to determine the " <<
                        "photometric representation for " << im->nc << 
                        "channels" << std::endl;
                return SIFT3D_FAILURE;
        }
        dataset->putAndInsertString(DCM_PhotometricInterpretation,
                photoInterp);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "photometric interpretation" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the pixel representation to unsigned
        dataset->putAndInsertUint16(DCM_PixelRepresentation, 0);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "pixel representation" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the number of channels (Samples Per Pixel) and set the planar
        // configuration to interlaced pixels
        assert(SIFT3D_IM_GET_IDX(im, 0, 0, 0, 1) == 
                SIFT3D_IM_GET_IDX(im, 0, 0, 0, 0) + 1);
        snprintf(buf, BUF_LEN, "%d", im->nc);
        dataset->putAndInsertString(DCM_SamplesPerPixel, buf);
        dataset->putAndInsertString(DCM_PlanarConfiguration, "0");

        // Set the bits allocated and stored, in big endian format 
        const unsigned int dcm_high_bit = dcm_bit_width - 1;
        dataset->putAndInsertUint16(DCM_BitsAllocated, dcm_bit_width);
        dataset->putAndInsertUint16(DCM_BitsStored, dcm_bit_width);
        dataset->putAndInsertUint16(DCM_HighBit, dcm_high_bit);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "bit widths" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the patient name
        status = dataset->putAndInsertString(DCM_PatientName, 
                meta_new.patient_name);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "patient name" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the patient ID
        status = dataset->putAndInsertString(DCM_PatientID,
                meta_new.patient_id);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "patient ID" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the study UID
        status = dataset->putAndInsertString(DCM_StudyInstanceUID,
                meta_new.study_uid);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "StudyInstanceUID" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the series UID
        status = dataset->putAndInsertString(DCM_SeriesInstanceUID,
                meta_new.series_uid);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "SeriesInstanceUID" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the series description
        status = dataset->putAndInsertString(DCM_SeriesDescription,
                meta_new.series_descrip);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "series description" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the instance UID
        status = dataset->putAndInsertString(DCM_SOPInstanceUID, 
                meta_new.instance_uid);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the " <<
                        "SOPInstanceUID"  << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the dimensions
        OFCondition xstatus = dataset->putAndInsertUint16(DCM_Rows, im->ny); 
        OFCondition ystatus = dataset->putAndInsertUint16(DCM_Columns, im->nx);
        snprintf(buf, BUF_LEN, "%d", im->nz);
        OFCondition zstatus = dataset->putAndInsertString(DCM_NumberOfFrames,
                buf);
        if (xstatus.bad() || ystatus.bad() || zstatus.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the dimensions " <<
                        std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the instance number
        snprintf(buf, BUF_LEN, "%u", meta_new.instance_num);
        status = dataset->putAndInsertString(DCM_InstanceNumber, buf);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the instance " <<
                        "number" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the slice location
        const double slice_loc = 
                im->uz * ((double) meta_new.instance_num - 1.0);
        snprintf(buf, BUF_LEN, "%f", slice_loc);
        status = dataset->putAndInsertString(DCM_SliceLocation, buf);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the slice "
                        "location" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the pixel spacing
        snprintf(buf, BUF_LEN, "%f\\%f", im->ux, im->uy);
        status = dataset->putAndInsertString(DCM_PixelSpacing, buf);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the pixel " <<
                        "spacing" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the aspect ratio
        snprintf(buf, BUF_LEN, "%f\\%f", im->ux, im->uy);
        status = dataset->putAndInsertString(DCM_PixelAspectRatio, buf);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the pixel " <<
                        "aspect ratio" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Set the slice thickness
        snprintf(buf, BUF_LEN, "%f", im->uz);
        status = dataset->putAndInsertString(DCM_SliceThickness, buf);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the slice " <<
                        "thickness" << std::endl;
                return SIFT3D_FAILURE;
        }

        // Count the number of pixels in the image
        unsigned long numPixels = im->dims[0];
        for (int i = 1; i < IM_NDIMS; i++) {
                numPixels *= im->dims[i];
        }

        // Get the image scaling factor
        const float dcm_max_val = static_cast<float>(1 << dcm_bit_width) - 1.0f;
        const float im_max = max_val < 0.0f ? im_max_abs(im) : max_val;
        const float scale = im_max == 0.0f ? 1.0f : dcm_max_val / im_max;

        // Render the data to an 8-bit unsigned integer array
        assert(dcm_bit_width == 8);
        assert(fabsf(dcm_max_val - 255.0f) < FLT_EPSILON);
        uint8_t *pixelData = new uint8_t[numPixels];
        int x, y, z, c;
        SIFT3D_IM_LOOP_START_C(im, x, y, z, c)

                const float vox = SIFT3D_IM_GET_VOX(im, x, y, z, c);

                if (vox < 0.0f) {
                        std::cerr << "write_dcm_cpp: Image cannot be " <<
                                "negative" << std::endl;                
                        return SIFT3D_FAILURE;
                }

                pixelData[c + x + y * im->nx + z * im->nx * im->ny] =
                        static_cast<uint8_t>(vox * scale);
        SIFT3D_IM_LOOP_END_C

        // Write the data
        status = dataset->putAndInsertUint8Array(DCM_PixelData, pixelData, 
                numPixels);
        delete pixelData;
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to set the pixel data " <<
                        std::endl;
                return SIFT3D_FAILURE;
        }

        // Choose the encoding format
#if 0
        DJEncoderRegistration::registerCodecs();
        const E_TransferSyntax xfer = EXS_JPEGProcess14SV1TransferSyntax;
        DJ_RPLossless rp_lossless;
        status = dataset->chooseRepresentation(xfer, &rp_lossless);
#else
        const E_TransferSyntax xfer = EXS_LittleEndianExplicit;
        dataset->chooseRepresentation(xfer, NULL);
#endif
        if (!dataset->canWriteXfer(xfer)) {
                std::cerr << "write_dcm_cpp: Failed to choose the encoding " <<
                        "format " << std::endl;
                return SIFT3D_FAILURE;
        }

        // Force the media storage UIDs to be re-generated by removing them
        dataset->remove(DCM_MediaStorageSOPClassUID);
        dataset->remove(DCM_MediaStorageSOPInstanceUID);

        // Save the file
        status = fileFormat.saveFile(path, xfer);
        if (status.bad()) {
                std::cerr << "write_dcm_cpp: Failed to write file " <<
                        path << " (" << status.text() << ")" << std::endl;
                return SIFT3D_FAILURE;
        }

        return SIFT3D_SUCCESS;
#undef BUF_LEN
}

/* Helper function to write an image to a directory of DICOM files using C++ */
static int write_dcm_dir_cpp(const char *path, const Image *const im,
        const Dcm_meta *const meta) {

        Image slice;

        // Initialize C intermediates
        init_im(&slice);

        // Initialize the metadata to defaults, if it is null 
        Dcm_meta meta_new;
        set_meta_defaults(meta, &meta_new);

        // Get the number of leading zeros for the file names
        const int num_slices = im->nz;
        const int num_zeros = static_cast<int>(ceil(log10(
                static_cast<double>(num_slices))));

        // Form the printf format string for file names
#define BUF_LEN 16
        char format[BUF_LEN];
        snprintf(format, BUF_LEN, "%%0%dd.%s", num_zeros, ext_dcm); 
#undef BUF_LEN

        // Resize the slice buffer
        slice.nx = im->nx; 
        slice.ny = im->ny;
        slice.nz = 1;
        slice.nc = im->nc;
        im_default_stride(&slice);
        if (im_resize(&slice)) {
                im_free(&slice);
                return SIFT3D_FAILURE;
        }

        // Get the maximum absolute value of the whole image volume
        const float max_val = im_max_abs(im);

        // Write each slice
        for (int i = 0; i < num_slices; i++) {

                // Form the slice file name
#define BUF_LEN 1024
                char buf[BUF_LEN];
                snprintf(buf, BUF_LEN, format, i);

                // Form the full file path
                std::string fullfile(path + sepStr + buf);

                // Copy the data to the slice
                int x, y, z, c;
                SIFT3D_IM_LOOP_START_C(&slice, x, y, z, c)
                        SIFT3D_IM_GET_VOX(&slice, x, y, z, c) =
                                SIFT3D_IM_GET_VOX(im, x, y, i, c);
                SIFT3D_IM_LOOP_END_C

                // Generate a new SOPInstanceUID
                dcmGenerateUniqueIdentifier(meta_new.instance_uid, 
                        SITE_INSTANCE_UID_ROOT); 

                // Set the instance number
                const unsigned int instance = static_cast<unsigned int>(i + 1);
                meta_new.instance_num = instance;

                // Write the slice to a file
                if (write_dcm(fullfile.c_str(), &slice, &meta_new, max_val)) {
                        im_free(&slice);
                        return SIFT3D_FAILURE;
                }
        }

        // Clean up
        im_free (&slice);

        return SIFT3D_SUCCESS;
}


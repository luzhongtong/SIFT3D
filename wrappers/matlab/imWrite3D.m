function imWrite3D(path, im)
%imRead3D(im) Write a 3D image to a file.
%  Arguments:
%    path - The path to the file.
%    im - An [MxNxP] array containing the image data, where the voxels 
%       are indexed in (x, y, z) order.
%
%  Supported file formats:
%    NIFTI (.nii, .nii.gz)
%    DICOM (.dcm)
%    Directory of DICOM files (no extension)
%
%  Examples:
%    imWrite3D('image.nii.gz', im); % NIFTI
%    imWrite3D('image.dcm', im); % Multi-slice DICOM
%    imWrite3D('image', im); % Directory of DICOM slices
%
%  See also:
%    imRead3D, detectSift3D, extractSift3D, setupSift3D
%
% Copyright (c) 2015 Blaine Rister et al., see LICENSE for details.

% Verify inputs
if nargin < 1 || isempty(path)
    error('path not specified')
end

if nargin < 2 || isempty(im)
    error('im not specified')
end

if ndims(im) > 3
   error(['im has invalid dimensionality: ' num2str(ndims(im))]) 
end

% Scale and convert the image to single-precision
im = im / max(im(:));
im = single(im);

% Write the image
mexImWrite3D(path, im);

end
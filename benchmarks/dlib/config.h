

// If you are compiling dlib as a shared library and installing it somewhere on your system
// then it is important that any programs that use dlib agree on the state of the
// DLIB_ASSERT statements (i.e. they are either always on or always off).  Therefore,
// uncomment one of the following lines to force all DLIB_ASSERTs to either always on or
// always off.  If you don't define one of these two macros then DLIB_ASSERT will toggle
// automatically depending on the state of certain other macros, which is not what you want
// when creating a shared library.
// #define ENABLE_ASSERTS       // asserts always enabled 
/* #undef DLIB_DISABLE_ASSERTS */

/* #undef DLIB_ISO_CPP_ONLY */
/* #undef DLIB_NO_GUI_SUPPORT */
#define DLIB_ENABLE_STACK_TRACE

#define LAPACK_FORCE_UNDERSCORE
/* #undef LAPACK_FORCE_NOUNDERSCORE */

// You should also consider telling dlib to link against libjpeg, libpng, libgif, fftw, CUDA, 
// and a BLAS and LAPACK library.  To do this you need to uncomment the following #defines.
#define DLIB_JPEG_SUPPORT
#define DLIB_PNG_SUPPORT
/* #undef DLIB_GIF_SUPPORT */
/* #undef DLIB_USE_FFTW */
#define DLIB_USE_BLAS
#define DLIB_USE_LAPACK
/* #undef DLIB_USE_CUDA */
/* #undef DLIB_USE_MKL_FFT */

// This variable allows dlib/test_for_odr_violations.h to catch people who mistakenly use
// headers from one version of dlib with a compiled dlib binary from a different dlib version.
// #define DLIB_CHECK_FOR_VERSION_MISMATCH DLIB_VERSION_MISMATCH_CHECK__EXPECTED_VERSION_19_21_99


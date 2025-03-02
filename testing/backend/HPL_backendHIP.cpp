
#include <hpl.h>


void HIP::init(size_t num_gpus)
{
    int rank, size, count;    
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    hipDeviceProp_t hipDeviceProp;
    HIP_CHECK_ERROR(hipGetDeviceCount(&count));
    
    //TODO: set dynamic device id
    int device_id = 0; 

    HIP_CHECK_ERROR(hipSetDevice(device_id));

    // Get device properties
    HIP_CHECK_ERROR(hipGetDeviceProperties(&hipDeviceProp, device_id));

    GPUInfo("%-25s %-12s \t%-5s", "[Device]", "Using HIP Device",  hipDeviceProp.name, "With Properties:");
    GPUInfo("%-25s %-20lld", "[GlobalMem]", "Total Global Memory",  (unsigned long long int)hipDeviceProp.totalGlobalMem);
    GPUInfo("%-25s %-20lld", "[SharedMem]", "Shared Memory Per Block", (unsigned long long int)hipDeviceProp.sharedMemPerBlock);
    GPUInfo("%-25s %-20d", "[Regs]", "Registers Per Block", hipDeviceProp.regsPerBlock);
    GPUInfo("%-25s %-20d", "[WarpSize]", "WaveFront Size", hipDeviceProp.warpSize);
    GPUInfo("%-25s %-20d", "[MaxThreads]", "Max Threads Per Block", hipDeviceProp.maxThreadsPerBlock);
    GPUInfo("%-25s %-4d %-4d %-4d", "[MaxThreadsDim]", "Max Threads Dimension", hipDeviceProp.maxThreadsDim[0], hipDeviceProp.maxThreadsDim[1], hipDeviceProp.maxThreadsDim[2]);
    GPUInfo("%-25s %-4d %-4d %-4d", "[MaxGridSize]", "Max Grid Size", hipDeviceProp.maxGridSize[0], hipDeviceProp.maxGridSize[1], hipDeviceProp.maxGridSize[2]);
    GPUInfo("%-25s %-20lld", "[ConstMem]", "Total Constant Memory",  (unsigned long long int)hipDeviceProp.totalConstMem);
    GPUInfo("%-25s %-20d", "[Major]", "Major", hipDeviceProp.major);
    GPUInfo("%-25s %-20d", "[Minor]", "Minor", hipDeviceProp.minor);
    GPUInfo("%-25s %-20d", "[ClkRate]", "Clock Rate", hipDeviceProp.memoryClockRate);
    GPUInfo("%-25s %-20d", "[#CUs]", "Multi Processor Count", hipDeviceProp.multiProcessorCount);
    GPUInfo("%-25s %-20d", "[PCIBusID]", "PCI Bus ID", hipDeviceProp.pciBusID);
    GPUInfo("----------------------------------------", "----------------------------------------");


    //Init ROCBlas
    rocblas_initialize();
    ROCBLAS_CHECK_STATUS(rocblas_create_handle(&_handle));

    _memcpyKind[0] = "H2H";
    _memcpyKind[1] = "H2D";
    _memcpyKind[2] = "D2H";
    _memcpyKind[3] = "D2D";
    _memcpyKind[4] = "DEFAULT";
}

void HIP::release()
{
    ROCBLAS_CHECK_STATUS(rocblas_destroy_handle(_handle));
}

void HIP::malloc(void** ptr, size_t size)
{
    GPUInfo("%-25s %-12ld (B) \t%-5s", "[Allocate]", "Memory of size",  size, "HIP");
    HIP_CHECK_ERROR(hipMalloc(ptr, size));
}

void HIP::free(void** ptr)
{
    HIP_CHECK_ERROR(hipFree(*ptr));
}

int HIP::panel_free(HPL_T_panel *ptr)
{
    GPUInfo("%-40s \t%-5s", "[Deallocate]", "Panel resources", "HIP");
    if( ptr->WORK  ) HIP_CHECK_ERROR(hipFree( ptr->WORK  ));
    if( ptr->IWORK ) HIP_CHECK_ERROR(hipFree( ptr->IWORK ));
    return( MPI_SUCCESS );
}

int HIP::panel_disp(HPL_T_panel **ptr)
{
    GPUInfo("%-40s \t%-5s", "[Deallocate]", "Panel structure", "HIP");
    int err = HIP::panel_free(*ptr);
    if(*ptr) HIP_CHECK_ERROR(hipFree( ptr ));
    *ptr = NULL;
    return( err );
}

void gPrintMat(const int M, const int N, const int LDA, const double *A)
{
    // Last row is the vector b
    for(int y=0;y<M+1; y++){
        for(int x=0;x<N-1; x++){
            int index = x+y*LDA;
            printf("%-4d:%-8lf\t", index, A[index]);
        }
        printf("\n");
    }
}

void HIP::matgen(const HPL_T_grid *GRID, const int M, const int N,
                 const int NB, double *A, const int LDA,
                 const int ISEED)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[Generate matrix]", "With A of (R:C)", M, N, "HIP");
    int mp, mycol, myrow, npcol, nprow, nq;
    (void) HPL_grid_info( GRID, &nprow, &npcol, &myrow, &mycol );
    
    Mnumroc( mp, M, NB, NB, myrow, 0, nprow );
    Mnumroc( nq, N, NB, NB, mycol, 0, npcol );

    if( ( mp <= 0 ) || ( nq <= 0 ) ) return;
    mp = (mp<LDA) ? LDA : mp;
    
    rocrand_generator generator;
    ROCRAND_CHECK_STATUS(rocrand_create_generator(&generator, ROCRAND_RNG_PSEUDO_DEFAULT)); // ROCRAND_RNG_PSEUDO_DEFAULT));
    ROCRAND_CHECK_STATUS(rocrand_set_seed(generator, ISEED));

    //TODO: generate numbers in this range (-0.5, 0.5]
    ROCRAND_CHECK_STATUS(rocrand_generate_normal_double(generator, A, mp*nq, 0, 0.1));
    ROCRAND_CHECK_STATUS(rocrand_destroy_generator(generator));
    //gPrintMat(5,5,LDA,A);
}

int HIP::idamax(const int N, const double *DX, const int INCX)
{
    GPUInfo("%-25s %-17d \t%-5s", "[IDAMAX]", "With X of (R)", N, "HIP");
    rocblas_int result;
    ROCBLAS_CHECK_STATUS(rocblas_idamax(_handle, N, DX, INCX, &result));
    return result;
}

void HIP::daxpy(const int N, const double DA, const double *DX, const int INCX, double *DY, 
                const int INCY)
{
    GPUInfo("%-25s %-17d \t%-5s", "[DAXPY]", "With X of (R)", N, "HIP");
    ROCBLAS_CHECK_STATUS(rocblas_daxpy(_handle, N, &DA, DX, INCX, DY, INCY));
}

void HIP::dscal(const int N, const double DA, double *DX, const int INCX)
{
    GPUInfo("%-25s %-17d \t%-5s", "[DSCAL]", "With X of (R)", N, "HIP");
    ROCBLAS_CHECK_STATUS(rocblas_dscal(_handle, N, &DA, DX, INCX));
}

void HIP::dswap(const int N, double *DX, const int INCX, double *DY, const int INCY)
{    
    GPUInfo("%-25s %-17d \t%-5s", "[DSWAP]", "With X of (R)", N, "HIP");
    ROCBLAS_CHECK_STATUS(rocblas_dswap(_handle, N, DX, INCX, DY, INCY));
}

void HIP::dger( const enum HPL_ORDER ORDER, const int M, const int N, const double ALPHA, const double *X,
               const int INCX, double *Y, const int INCY, double *A, const int LDA)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[DGER]", "With A of (R:C)", M, N, "HIP");
    //rocBLAS uses column-major storage for 2D arrays
    ROCBLAS_CHECK_STATUS(rocblas_dger(_handle, M, N, &ALPHA, X, INCX, Y, INCY, A, LDA));
}

void HIP::trsm( const enum HPL_ORDER ORDER, const enum HPL_SIDE SIDE, 
                const enum HPL_UPLO UPLO, const enum HPL_TRANS TRANSA, 
                const enum HPL_DIAG DIAG, const int M, const int N, 
                const double ALPHA, const double *A, const int LDA, double *B, const int LDB)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[TRSM]", "With B of (R:C)", M, N, "HIP");
#if 0
    //rocBLAS uses column-major storage for 2D arrays
    ROCBLAS_CHECK_STATUS(rocblas_dtrsm(_handle, (rocblas_side)SIDE, (rocblas_fill)UPLO, (rocblas_operation)TRANSA, 
                  (rocblas_diagonal)DIAG, M, N, &ALPHA, A, LDA, B, LDB));
#else
    double * d_A, * d_B;
    HIP::malloc((void**)&d_A, LDA*M*sizeof(double));
    HIP::malloc((void**)&d_B, LDB*N*sizeof(double));

    HIP::move_data(d_A, A, LDA*M*sizeof(double), 1);
    HIP::move_data(d_B, B, LDB*N*sizeof(double), 1);
    
    ROCBLAS_CHECK_STATUS(rocblas_dtrsm(_handle, (rocblas_side)SIDE, (rocblas_fill)UPLO, (rocblas_operation)TRANSA, 
                  (rocblas_diagonal)DIAG, M, N, &ALPHA, d_A, LDA, d_B, LDB));
                  
    HIP::move_data(B, d_B, LDB*N*sizeof(double), 2);

    HIP::free((void**)&d_A);
    HIP::free((void**)&d_B);
#endif
}

void HIP::trsv(const enum HPL_ORDER ORDER, const enum HPL_UPLO UPLO,
                const enum HPL_TRANS TRANSA, const enum HPL_DIAG DIAG,
                const int N, const double *A, const int LDA,
                double *X, const int INCX)
{ 
    GPUInfo("%-25s %-17d \t%-5s", "[TRSV]", "With A of (R)", N, "HIP");
    //rocBLAS uses column-major storage for 2D arrays
    ROCBLAS_CHECK_STATUS(rocblas_dtrsv(_handle, (rocblas_fill)UPLO, (rocblas_operation)TRANSA,
                    (rocblas_diagonal)DIAG, N, A, LDA, X, INCX));
}

void HIP::dgemm(const enum HPL_ORDER ORDER, const enum HPL_TRANS TRANSA, 
                const enum HPL_TRANS TRANSB, const int M, const int N, const int K, 
                const double ALPHA, const double *A, const int LDA, 
                const double *B, const int LDB, const double BETA, double *C, 
                const int LDC)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[DGEMM]", "With C of (R:C)", LDC, N, "HIP");
#if 0
    //rocBLAS uses column-major storage for 2D arrays
    ROCBLAS_CHECK_STATUS(rocblas_dgemm(_handle, (rocblas_operation)TRANSA, (rocblas_operation)TRANSB, 
                         M, N, K, &ALPHA, A, LDA, B, LDB, &BETA, C, LDC));
#else
    double                    * d_A, * d_B, * d_C;
    HIP::malloc((void**)&d_A, LDA*K*sizeof(double));
    HIP::malloc((void**)&d_B, LDB*N*sizeof(double));
    HIP::malloc((void**)&d_C, LDC*N*sizeof(double));

    HIP::move_data(d_A, A, LDA*K*sizeof(double), 1);
    HIP::move_data(d_B, B, LDB*N*sizeof(double), 1);
    HIP::move_data(d_C, C, LDC*N*sizeof(double), 1);

    ROCBLAS_CHECK_STATUS(rocblas_dgemm(_handle, (rocblas_operation)TRANSA, (rocblas_operation)TRANSB, 
                         M, N, K, &ALPHA, d_A, LDA, d_B, LDB, &BETA, d_C, LDC));

    HIP::move_data(C, d_C, LDC*N*sizeof(double), 2);

    HIP::free((void**)&d_A);
    HIP::free((void**)&d_B);
    HIP::free((void**)&d_C);
#endif

    hipDeviceSynchronize();                         
}

void HIP::dgemv(const enum HPL_ORDER ORDER, const enum HPL_TRANS TRANS, const int M, const int N,
                const double ALPHA, const double *A, const int LDA, const double *X, const int INCX,
                const double BETA, double *Y, const int INCY)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[DGEMV]", "With A of (R:C)", M, N, "HIP");
    //rocBLAS uses column-major storage for 2D arrays
    ROCBLAS_CHECK_STATUS(rocblas_dgemv(_handle, (rocblas_operation)TRANS,M, N, &ALPHA, A, LDA, X, INCX, &BETA, Y, INCY));
}

/*
*  ----------------------------------------------------------------------
*  - COPY ---------------------------------------------------------------
*  ----------------------------------------------------------------------
*/ 
void HIP::copy(const int N, const double *X, const int INCX, double *Y, const int INCY)
{
    GPUInfo("%-25s %-17d \t%-5s", "[COPY]", "With X of (R)", N, "HIP");
    ROCBLAS_CHECK_STATUS(rocblas_dcopy(_handle, N, X, INCX, Y, INCY));
}

__global__ void 
_dlacpy(const int M, const int N, const double *A, const int LDA,
        double *B, const int LDB)
{
 
}

/*
* Copies an array A into an array B.
*/
void HIP::acpy(const int M, const int N, const double *A, const int LDA,
                  double *B, const int LDB)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[LACOPY]", "With A of (R:C)", M, N, "HIP");
    dim3 block_size(64, 1);
    dim3 grid_size((M+64-1)/64, (N+64-1)/64);
    _dlacpy<<<block_size, grid_size, 0, 0>>>(M, N, A, LDA, B, LDB);
}

#define TILE_DIM 64
#define BLOCK_ROWS 16

__global__ void 
__launch_bounds__(TILE_DIM *BLOCK_ROWS)  
_dlatcpy(const int M, const int N, const double* __restrict__ A, const int LDA,
         double* __restrict__ B, const int LDB) 
{

}

/*
* Copies the transpose of an array A into an array B.
*/
void HIP::atcpy(const int M, const int N, const double *A, const int LDA,
                double *B, const int LDB)
{
    GPUInfo("%-25s %-8d%-8d \t%-5s", "[LATCOPY]", "With A of (R:C)", M, N, "HIP");
    dim3 grid_size((M+TILE_DIM-1)/TILE_DIM, (N+TILE_DIM-1)/TILE_DIM);
    dim3 block_size(TILE_DIM, BLOCK_ROWS);
    _dlatcpy<<<grid_size, block_size, 0, 0>>>(M, N, A, LDA, B, LDB);
}

void HIP::move_data(double *DST, const double *SRC, const size_t SIZE, const int KIND)
{
    char title[25] = "[MOVE_"; strcat(title,_memcpyKind[KIND]); strcat(title,"]");
    GPUInfo("%-25s %-12ld (B) \t%-5s", title, "Memory of size",  SIZE, "HIP");
    HIP_CHECK_ERROR(hipMemcpy(DST, SRC, SIZE, (hipMemcpyKind)KIND));
}

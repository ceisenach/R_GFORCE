#include "math.h"
#include "stdio.h"
#include "float.h"
#include "R.h"
#include "R_ext/Lapack.h"
#include "R_ext/BLAS.h"
#include "convex_kmeans_util.h"
#include "test_low_level.h"

static const char JOBZV = 'V';
static const char JOBZN = 'N';
static const char UPLO = 'L';
static const char SIDE_L = 'L';
static const char SIDE_R = 'R';
static const char TRANS_N = 'N';
static const char TRANS_T = 'T';
static const double ALPHA = 1.0;
static const double BETA = 0.0;
static const int INC1 = 1;
static const int NRHS1 = 1;
static const double GNORM_FACTOR = 2.82842712475; //\sqrt{8}

// NOTE: All methods assume column major layout

// CANNOT OVERWRITE *X
// REQUIRES work -> dwork be of length at least min(dseyvd_ldwork + d,
//   2d^2 + d) (total should be 1 + 7d +2d^2)
// REQUIRES work -> iwork be of length at least dseyvd_ldwork (should be 3+5d)
void smoothed_gradient_nok(problem_instance* prob, double* X, double* GX_t, double* GS_t, 
                        workspace* work){

    // Grab Problem Parameters
    int d = prob -> d;
    double mu_n = prob -> mu_n;

    // Local Vars
    double* d2_tmp;
    double* d2_tmp2;
    double S_min;
    double X_min;
    double lambda_min_n;
    double scale_factor;
    double* X_eigs;
    int X_eigs_idx;
    int tmp;
    int d2 = d*d;
    int lapack_info;
    
    X_eigs_idx = work -> dsyevd_ldwork;
    tmp = 2*d2;
    X_eigs_idx = X_eigs_idx > tmp ? X_eigs_idx : tmp;
    X_eigs = (work -> dwork) + X_eigs_idx;
    d2_tmp = work -> dwork;
    d2_tmp2 = d2_tmp + d2;

    //eigenvectors will overwrite input matrix, so GX_t becomes V, eigenvectors
    F77_CALL(dsyevd)(&JOBZV,&UPLO,&d,GX_t,&d,X_eigs,work->dwork,&(work->ldwork),
                work->iwork,&(work->liwork),&lapack_info);
    // GS_t is just current value of X
    memcpy(GS_t,X,d2*sizeof(double));

    X_min = min_array(d,X_eigs);
    S_min = min_array(d2,GS_t);
    lambda_min_n = -1*(S_min < X_min ? S_min : X_min);

    // subtract lambda_min from all eigenvalues
    daps(X_eigs,1,lambda_min_n,d);
    daps(GS_t,1,lambda_min_n,d2);

    // Divide both eigenvalues by mu
    F77_NAME(drscl)(&d,&mu_n,X_eigs,&INC1);
    F77_NAME(drscl)(&d2,&mu_n,GS_t,&INC1);

    // Exponentiate eigenvalues
    dvexp(X_eigs,d);
    dvexp(GS_t,d2);

    // Compute Scaling Factor
    scale_factor = dsumv(X_eigs,d);
    scale_factor = scale_factor + dsumv(GS_t,d2);

    // Rescale X eigenvalues
    memcpy(d2_tmp,GX_t,d2*sizeof(double));
    memcpy(d2_tmp2,GX_t,d2*sizeof(double));
    dsmtd(d2_tmp,X_eigs,d,SIDE_R);//multiply V by diag(X_eigs), stored in dtmp2
    //neither matrix is symmetric, so need to use dgemm
    F77_NAME(dgemm)(&TRANS_N,&TRANS_T,&d,&d,&d,&ALPHA,d2_tmp,&d,d2_tmp2,&d,&BETA,GX_t,&d);

    // Rescale all eigenvalues by scaling factor
    F77_NAME(drscl)(&d2,&scale_factor,GX_t,&INC1);
    F77_NAME(drscl)(&d2,&scale_factor,GS_t,&INC1);
}

// X is untouched
// REQUIRES dwork 2d^2 + 3d + 1 (assuming dsyevd only needs 2d+1)
// REQUIRES iwork 1.
void smoothed_objective_nok(problem_instance* prob, double* X, double* lambda_min_tp1,
                            double* obj_tp1, workspace* work){
    // Grab Problem Parameters
    int d = prob -> d;
    double mu_n = prob -> mu_n;

    // Local Vars
    double* GS_t;
    double* d2_tmp;
    double S_min;
    double X_min;
    double lambda_min_n;
    double lambda_min;
    double obj_value;
    double* X_eigs;
    int X_eigs_idx;
    int tmp;
    int d2 = d*d;
    int lapack_info;
    
    X_eigs_idx = work -> dsyevd_ldwork_N;
    X_eigs = (work -> dwork) + X_eigs_idx;
    GS_t = X_eigs + d;
    d2_tmp = GS_t + d2;

    //eigenvectors will overwrite input matrix, so GX_t becomes V, eigenvectors
    F77_CALL(dsyevd)(&JOBZN,&UPLO,&d,d2_tmp,&d,X_eigs,work->dwork,&(work->dsyevd_ldwork_N),
                work->iwork,&(work->dsyevd_liwork_N),&lapack_info);
    // GS_t is just current value of X
    memcpy(GS_t,X,d2*sizeof(double));

    X_min = min_array(d,X_eigs);
    S_min = min_array(d2,GS_t);
    lambda_min_n = -1*(S_min < X_min ? S_min : X_min);
    *lambda_min_tp1 = lambda_min;


    // subtract lambda_min from all eigenvalues
    daps(X_eigs,1,lambda_min_n,d);
    daps(GS_t,1,lambda_min_n,d2);

    // Divide both eigenvalues by mu
    F77_NAME(drscl)(&d,&mu_n,X_eigs,&INC1);
    F77_NAME(drscl)(&d2,&mu_n,GS_t,&INC1);

    // Exponentiate eigenvalues
    dvexp(X_eigs,d);
    dvexp(GS_t,d2);

    // Compute Objective Value
    obj_value = dsumv(X_eigs,d);
    obj_value = obj_value + dsumv(GS_t,d2);
    obj_value = mu_n*log(obj_value) + lambda_min;

    *obj_tp1 = obj_value;
}

// REQUIRES work -> dwork be of length at least d^2 + 7d + 6?
// REQUIRES work -> iwork be of length at least d+2?
// b requires d+2
// M requires d^2 + 4d + 4
// S_rsums requires d
// X_rsums requires d
// Return value is stored as X_t
void C_perp_update(problem_instance* prob, double alpha, double* X_t, double* GX_t,
                    double* GS_t, workspace* work) {
    double g_norm;
    int d = prob -> d;
    int d2 = d*d;

    // compute projection -- gradient is partially overwritten
    project_C_perpendicular(prob, GX_t, GS_t, work);

    // compute norm and rescale
    g_norm = F77_NAME(dnrm2)(&d2,GX_t,&INC1);
    g_norm = GNORM_FACTOR*g_norm; // need to account for slack variables
    F77_NAME(drscl)(&d2,&g_norm,GX_t,&INC1);

    // update and return
    F77_NAME(daxpy)(&d2,&alpha,GX_t,&INC1,X_t,&INC1);
}

// Return value is stored in Z_proj
// Z IS NOT ALTERED
void project_E(problem_instance* prob, double* Z, double lmin, double* Z_proj){
    int d = prob -> d;
    int d2 = d*d;
    double* E = prob -> E;
    double scale_factor = 1/(1-lmin);

    memcpy(Z_proj,Z,d2*sizeof(double));
    F77_NAME(dscal)(&d2,&scale_factor,Z_proj,&INC1);
    scale_factor = 1 - scale_factor;
    F77_NAME(daxpy)(&d2,&scale_factor,E,&INC1,Z_proj,&INC1);
}


// OUTPUT STORED IN GX_T
void project_C_perpendicular(problem_instance* prob, double* GX_t, double* GS_t, workspace* work){
    // Local Declarations
    int lapack_info;
    double scale_factor;
    double dtmp1, dtmp2;
    double* ptmp1;

    // Problem Instance Extraction, Workspace setup
    double* D_rsums = prob -> D_rsums;
    double* D = prob -> D;
    double TrD = prob -> TrD;
    double DTD = prob -> DTD;
    int d = prob -> d;
    int* ipiv = work -> iwork;
    int dp2 = d+2;
    int dp1 = d+1;
    int dp3 = d+3;
    int dp22 = dp2*dp2;
    int d2 = d*d;

    // Setup linear system, precompute
    double TrX = dtrace(GX_t,d);
    double TrS = dtrace(GS_t,d);
    double DTX = F77_NAME(ddot)(&d2,D,&INC1,GX_t,&INC1);
    double DTS = F77_NAME(ddot)(&d2,D,&INC1,GS_t,&INC1);
    double* M = work -> dwork;
    double* b = M + dp22;
    double* S_rsums = b + dp2;
    double* X_rsums = S_rsums + d;
    dcsum(GX_t,d,X_rsums);
    dcsum(GS_t,d,S_rsums);

    // M all ones
    ptmp1 = M;
    for(int i=0; i < dp22; i++){
        *ptmp1 = 1;
        ptmp1++;
    }
    //M <- M + p*diag(p+2)
    ptmp1 = M;
    for(int i=0; i < d; i++){
        *ptmp1 = dp1;
        ptmp1 = ptmp1 + dp3;
    }

    // M[1:p,p+2] <- rowSums(C)
    memcpy(M + dp1*dp2,D_rsums,d*sizeof(double));
    //b[1:p] <- rowSums(Z) + rowSums(S)
    dxpyez(d,X_rsums,S_rsums,b);
    //M[p+1,1:p] <- 2*rep(1,p)
    ptmp1 = M + d;
    for(int i=0; i < d; i++){
        *ptmp1 = 2;
        ptmp1 = ptmp1 + dp2;
    }
    // M[p+1,p+1] <- p
    *(M + d + d*dp2) = d;
    // M[p+1,p+2] <- TC
    *(M + d + (dp2-1)*dp2) = TrD;
    // b[p+1] <- sum(diag(Z)) + k3*sum(diag(S))
    *(b+d) = TrX + TrS;
    // M[p+2,1:p] <- 2*colSums(C)
    scale_factor = 2.0;
    ptmp1 = M+d+1;
    for(int i=0; i < d; i++){
        dtmp1 = (*D_rsums)*scale_factor;
        *ptmp1 = dtmp1;
        ptmp1 = ptmp1+dp2;
        D_rsums++;
    }
    // M[p+2,p+1] <- TC
    *(M + dp1 + d*dp2) = TrD;
    // M[p+2,p+2] <- sum(C*C)
    *(M + dp22 - 1) = DTD;
    // b[p+2] <- sum(C*Z) + k3*sum(C*S)
    *(b+dp1) = DTX + DTS;

    // b will store x, the solution
    F77_CALL(dgesv)(&dp2,&NRHS1,M,&dp2,ipiv,b,&dp2,&lapack_info);

    // multiply GX by 1/2 and store in GX
    scale_factor = 0.5;
    F77_NAME(dscal)(&d2,&scale_factor,GX_t,&INC1);

    // 0.5GX + 0.5GS -> GX
    F77_NAME(daxpy)(&d2,&scale_factor,GS_t,&INC1,GX_t,&INC1);

    // GX - 0.5*x[d+2]*D
    scale_factor = -0.5;
    dtmp1 = scale_factor * (*(b+d+1));
    F77_NAME(daxpy)(&d2,&dtmp1,D,&INC1,GX_t,&INC1);

    //GX - 0.5*x[d+1]*I
    dtmp1 = scale_factor * (*(b+d));
    ptmp1 = GX_t;
    for(int i=0; i < d; i++){
        dtmp2 = (*ptmp1) + dtmp1;
        *ptmp1 = dtmp2;
        ptmp1 = ptmp1 + dp1;
    }

    //GX -0.5*x[a]*R_a
    for(int a=0; a < d; a++){
        dtmp1 = scale_factor*(*(b+a));
        daps(GX_t + a*d,1,dtmp1,d);
        daps(GX_t + a,d,dtmp1,d);
    }
}